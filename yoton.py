#!/usr/bin/env python3
"""Host tool for the Yoton (Actions ATJ2157) via the ADFU boot ROM + ymon.

  yoton.py state                       report rom / ymon / none
  yoton.py run PAYLOAD.bin [-o OUT] [--args a,b,..]
                                       upload to 0x11e000, call it (fault-protected),
                                       fetch its {addr,size} result
  yoton.py read ADDR SIZE OUT          read any memory (ROM, SRAM, registers)
  yoton.py write ADDR FILE             write memory
  yoton.py nordump ADDR SIZE OUT       read SPI NOR (payloads/nordump.bin)
  yoton.py reset                       system reset (device returns to ADFU)

Payload convention: entry returns a pointer to {void *addr; uint32_t size};
arguments (up to 16 words) are placed at ARGS = 0x11f800 before the call.
"""
import argparse, os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import actions_probe as ap

YMON = os.path.join(HERE, "payloads/ymon.bin")
YMON_ADDR = 0x118000
PAYLOAD_ADDR = 0x11E000
ARGS_ADDR = 0x11F800


def _find():
    import usb.core
    return usb.core.find(idVendor=0x10D6, idProduct=0x10D6)


def wait_dev(timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if _find() is not None:
            time.sleep(0.5)
            return True
        time.sleep(0.25)
    return False


class Dev:
    def __init__(self):
        if not wait_dev(5):
            sys.exit("no ADFU device (10d6:10d6)")
        self.t = ap.BOTTransport(0x10D6)

    # --- raw ADFU transfer (tag 0), tolerant variant for probing
    def xfer(self, sub, length, addr, data_out=None, data_in=0, timeout=5000, check=True):
        t = self.t
        flags = 0x80 if data_in else 0x00
        dlen = data_in if data_in else (len(data_out) if data_out else 0)
        cbw = struct.pack("<IIIBBB", 0x43425355, 0, dlen, flags, 0, 16) + bytes(ap.adfu_cmd_cdb(sub, length, addr))
        t.ep_out.write(cbw, timeout)
        if data_out:
            t.ep_out.write(data_out, timeout)
        buf = b""
        if data_in:
            buf = bytes(t.ep_in.read(data_in + 13, timeout))
            if len(buf) == data_in:
                buf += bytes(t.ep_in.read(13, timeout))
            data, csw = buf[:-13], buf[-13:]
        else:
            data, csw = b"", bytes(t.ep_in.read(13, timeout))
        ok = len(csw) == 13 and csw[:4] == b"USBS"
        status = csw[12] if ok else -1
        if check and status != 0:
            raise RuntimeError(f"cmd {sub:#x} @ {addr:#x}: CSW status {status} ({csw.hex()})")
        return status, data

    def state(self):
        try:
            st, data = self.xfer(0x7E, 16, 0, data_in=16, timeout=800, check=False)
        except Exception:
            try:
                self.t.dev.clear_halt(self.t.ep_in)
            except Exception:
                pass
            return None
        if st == 0 and data[:4] == b"YMON":
            return "ymon"
        return "rom" if st in (1, 2) else None

    def read(self, addr, n):
        out = b""
        while len(out) < n:
            c = min(0x10000, n - len(out))
            out += self.xfer(0x93, c, addr + len(out), data_in=c)[1]
        return out

    def write(self, addr, data, step=0x200):
        for i in range(0, len(data), step):
            chunk = data[i:i + step]
            self.xfer(0x13, len(chunk), addr + i, data_out=chunk)

    def ensure_ymon(self):
        st = self.state()
        if st == "ymon":
            return
        if st is None:
            print("[device not answering: USB reset]", file=sys.stderr)
            try:
                self.t.dev.reset()
            except Exception:
                pass
            time.sleep(1)
            if not wait_dev(90):
                sys.exit("device did not come back")
            self.t = ap.BOTTransport(0x10D6)
            st = self.state()
            if st == "ymon":
                return
            if st != "rom":
                sys.exit("device in unknown state")
        blob = open(YMON, "rb").read()
        self.write(YMON_ADDR, blob)
        if self.read(YMON_ADDR, len(blob)) != blob:
            sys.exit("ymon upload verify failed")
        self.xfer(0x20, 0, YMON_ADDR | 1)
        time.sleep(0.05)
        if self.state() != "ymon":
            sys.exit("ymon did not start")
        print("[ymon started]", file=sys.stderr)

    def fault(self):
        f = struct.unpack("<8I", self.xfer(0x24, 32, 0, data_in=32)[1])
        names = "magic cfsr hfsr mmfar bfar pc lr xpsr".split()
        return dict(zip(names, f))

    def load(self, blob, addr=PAYLOAD_ADDR):
        self.write(addr, blob)
        if self.read(addr, len(blob)) != blob:
            sys.exit("payload verify failed")

    def call(self, addr=PAYLOAD_ADDR, args=(), timeout=20000):
        if args:
            self.write(ARGS_ADDR, struct.pack(f"<{len(args)}I", *args))
        st, _ = self.xfer(0x21, 0, addr | 1, check=False)
        if st != 0:
            raise RuntimeError(f"exec start status {st}")
        t0 = time.time()
        while True:
            state = struct.unpack("<I", self.xfer(0x25, 4, 0, data_in=4)[1])[0]
            if state != 1:
                break
            if (time.time() - t0) * 1000 > timeout:
                self.xfer(0x26, 0, 0, check=False)
                time.sleep(0.05)
                state = struct.unpack("<I", self.xfer(0x25, 4, 0, data_in=4)[1])[0]
                raise RuntimeError(f"payload timed out after {timeout} ms; aborted (state {state})")
            time.sleep(0.01)
        if state == 3:
            f = self.fault()
            raise RuntimeError("payload faulted: " + " ".join(f"{k}={v:#x}" for k, v in f.items()))
        if state != 2:
            raise RuntimeError(f"payload ended in state {state}")
        size = struct.unpack("<I", self.xfer(0x22, 4, 0, data_in=4)[1])[0]
        return self.xfer(0x23, size, 0, data_in=size, timeout=timeout)[1] if size else b""

    def run(self, blob, args=(), addr=PAYLOAD_ADDR):
        self.load(blob, addr)
        return self.call(addr, args)

    def reset(self):
        try:
            cbw = struct.pack("<IIIBBB", 0x43425355, 0, 0, 0, 0, 16) + bytes([0xB0] + [0] * 15)
            self.t.ep_out.write(cbw, 1000)
            self.t.ep_in.read(13, 1000)
        except Exception:
            pass


def rgb565_png(data, w, h, path):
    import zlib
    rows = b""
    for y in range(h):
        rows += b"\0"
        for x in range(w):
            v = data[2 * (y * w + x)] | data[2 * (y * w + x) + 1] << 8
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def to_adfu_from_msc(timeout=90):
    """If the stock firmware (10d6:1101) or Rockbox (10d6:1157) is running in
    USB disk mode, ask it to reboot to ADFU (SCSI 0xCC identify, 0xCB 0x21)."""
    import usb.core, glob
    t0 = time.time()
    while time.time() - t0 < timeout:
        if _find() is not None:
            return True
        if (usb.core.find(idVendor=0x10D6, idProduct=0x1101) is not None or
                usb.core.find(idVendor=0x10D6, idProduct=0x1157) is not None):
            time.sleep(3)
            for s in glob.glob("/sys/class/scsi_generic/sg*"):
                try:
                    v = open(s + "/device/vendor").read()
                    if "ACTIONS" in v or "Rockbox" in v:
                        t = ap.SGTransport("/dev/" + os.path.basename(s))
                        ap.adfu_reboot(t)
                        print("  stock firmware booted; sent reboot-to-ADFU")
                        break
                except Exception as e:
                    print("  msc reboot attempt:", e)
            time.sleep(5)
        time.sleep(0.5)
    return False


def nor_read_raw(d, addr, size):
    d.load(open(os.path.join(HERE, "payloads/nordump.bin"), "rb").read())
    out = b""
    while len(out) < size:
        n = min(0x8000, size - len(out))
        out += d.call(args=(addr + len(out), n, 0, 0, 0, 0))
    return out


def nor_write(d, addr, data):
    """Erase 4K sectors and program 256-byte pages; payload refuses anything
    outside 0x762000-0x7dffff. Verifies by reading back."""
    if addr & 0xfff:
        sys.exit("address must be 4K aligned")
    data = data + b"\xff" * (-len(data) % 0x1000)
    prog = open(os.path.join(HERE, "payloads/norprog.bin"), "rb").read()
    d.load(prog)
    t0 = time.time()
    for off in range(0, len(data), 0x1000):
        r = struct.unpack("<4I", d.call(args=(1, addr + off, 0)))
        if r[0] != 0:
            sys.exit(f"erase failed at {addr + off:#x}: {r}")
        for p in range(0, 0x1000, 256):
            chunk = data[off + p: off + p + 256]
            if chunk == b"\xff" * 256:
                continue
            d.write(0x122000, chunk)
            r = struct.unpack("<4I", d.call(args=(2, addr + off + p, 256)))
            if r[0] != 0:
                sys.exit(f"program failed at {addr + off + p:#x}: {r}")
    back = nor_read_raw(d, addr, len(data))
    if back != data:
        bad = next(i for i in range(len(data)) if back[i] != data[i])
        sys.exit(f"verify FAILED at {addr + bad:#x}")
    print(f"wrote+verified {len(data)} bytes at {addr:#x} in {time.time() - t0:.1f}s")


def num(x):
    return int(x, 0)


def main():
    p = argparse.ArgumentParser()
    s = p.add_subparsers(dest="op", required=True)
    s.add_parser("state")
    s.add_parser("reset")
    r = s.add_parser("run"); r.add_argument("payload"); r.add_argument("-o", "--out")
    r.add_argument("--args", default=""); r.add_argument("--addr", type=num, default=PAYLOAD_ADDR)
    rd = s.add_parser("read"); rd.add_argument("addr", type=num); rd.add_argument("size", type=num); rd.add_argument("out")
    b = s.add_parser("boot", help="upload a flat image and jump to it")
    b.add_argument("image"); b.add_argument("--addr", type=num, default=0x124000)
    b.add_argument("--test", action="store_true", help="arm bootloader test mode (auto reboot)")
    sc = s.add_parser("screenshot", help="framebuffer (RGB565 at 0x158000) to PNG")
    sc.add_argument("out"); sc.add_argument("--addr", type=num, default=0x158000)
    sc.add_argument("--size", default="128x128")
    bt = s.add_parser("boottest", help="boot a Rockbox bootloader in test mode, then read back status + screen")
    bt.add_argument("elf"); bt.add_argument("--png", default="ym03_screen.png")
    nw = s.add_parser("norwrite", help="erase+program the free NOR region (0x762000-0x7dffff) and verify")
    nw.add_argument("addr", type=num); nw.add_argument("file")
    w = s.add_parser("write"); w.add_argument("addr", type=num); w.add_argument("file")
    nd = s.add_parser("nordump"); nd.add_argument("addr", type=num); nd.add_argument("size", type=num); nd.add_argument("out")
    nd.add_argument("--descramble", action="store_true", help="read via ROM DMA path with HW descrambler")
    a = p.parse_args()
    d = Dev()
    if a.op == "state":
        print(d.state()); return
    if a.op == "reset":
        d.reset(); return
    if a.op == "screenshot":
        wd, ht = (int(v) for v in a.size.split("x"))
        d.ensure_ymon()
        rgb565_png(d.read(a.addr, wd * ht * 2), wd, ht, a.out)
        print("wrote", a.out); return
    d.ensure_ymon()
    if a.op == "run":
        args = [num(x) for x in a.args.split(",") if x]
        out = d.run(open(a.payload, "rb").read(), args, a.addr)
        if a.out:
            open(a.out, "wb").write(out)
        else:
            ap.hexdump(out)
    elif a.op == "read":
        open(a.out, "wb").write(d.read(a.addr, a.size))
    elif a.op == "boot":
        img = open(a.image, "rb").read()
        lo, hi = a.addr, a.addr + len(img)
        if lo < 0x11C000 and hi > 0x118000:
            sys.exit("image would overwrite ymon (0x118000-0x11bfff)")
        d.write(a.addr, img, step=0x1000)
        if d.read(a.addr, len(img)) != img:
            sys.exit("image verify failed")
        if a.test:
            d.write(0x11FFF0, struct.pack("<I", 0x54535459))
        d.xfer(0x20, 0, a.addr | 1)
        print(f"jumped to {a.addr:#x} ({len(img)} bytes)")
    elif a.op == "boottest":
        import subprocess
        binf = a.elf[:-4] + ".bin"
        nm = subprocess.run(["arm-none-eabi-nm", a.elf], capture_output=True, text=True).stdout
        st_addr = int([l for l in nm.splitlines() if l.endswith(" ym03_boot_status")][0].split()[0], 16)
        img = open(binf, "rb").read()
        d.write(st_addr & ~0xff, bytes(256))      # clear stale status
        d.write(0x124000, img, step=0x1000)
        if d.read(0x124000, len(img)) != img:
            sys.exit("image verify failed")
        d.write(0x11FFF0, struct.pack("<I", 0x54535459))
        d.xfer(0x20, 0, 0x124001)
        print(f"booted {binf}; waiting for test-mode reboot...")
        import usb.util
        usb.util.dispose_resources(d.t.dev)
        t0 = time.time()
        while _find() is not None and time.time() - t0 < 30:
            time.sleep(0.2)
        print(f"  device left the bus after {time.time() - t0:.1f}s")
        to_adfu_from_msc(90)
        if not wait_dev(60):
            sys.exit("device did not come back to ADFU")
        time.sleep(1)
        d = Dev(); d.ensure_ymon()
        st = struct.unpack("<I", d.read(st_addr, 4))[0]
        print(f"ym03_boot_status = {st:#x}")
        rgb565_png(d.read(0x158000, 128 * 128 * 2), 128, 128, a.png)
        print("screen ->", a.png)
    elif a.op == "norwrite":
        data = open(a.file, "rb").read()
        nor_write(d, a.addr, data)
    elif a.op == "write":
        d.write(a.addr, open(a.file, "rb").read())
    elif a.op == "nordump":
        pl = "payloads/nordump_rom.bin" if a.descramble else "payloads/nordump.bin"
        d.load(open(os.path.join(HERE, pl), "rb").read())
        CH = 0x8000
        t0 = time.time()
        with open(a.out, "wb") as fo:
            for off in range(0, a.size, CH):
                n = min(CH, a.size - off)
                fo.write(d.call(args=(a.addr + off, n, 0, 0, 0, int(a.descramble), 0)))
                if (off // CH) % 64 == 0:
                    print(f"{a.addr + off:#08x}  {time.time() - t0:.1f}s", file=sys.stderr)


if __name__ == "__main__":
    main()
