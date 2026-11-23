#!/usr/bin/env python3
"""Probe an Actions Semiconductor (VID 0x10d6) MP3 player over USB.

Two transports:
  * --sg /dev/sgN : SCSI pass-through via the kernel (SG_IO ioctl); the
                    usb-storage driver stays bound.
  * --usb         : raw USB Mass-Storage Bulk-Only Transport via pyusb.
                    Detaches usb-storage. Also works for non-MSC modes
                    (e.g. ADFU) as long as the device speaks CBW/CSW.

Only standard, read-only SCSI commands are sent unless a raw CDB is given
explicitly with `cdb`.
"""
import argparse
import ctypes
import fcntl
import os
import struct
import sys

VID = 0x10D6

# ----------------------------------------------------------------- SG_IO ---
SG_IO = 0x2285
SG_DXFER_NONE, SG_DXFER_TO_DEV, SG_DXFER_FROM_DEV = -1, -2, -3


class sg_io_hdr(ctypes.Structure):
    _fields_ = [
        ("interface_id", ctypes.c_int), ("dxfer_direction", ctypes.c_int),
        ("cmd_len", ctypes.c_ubyte), ("mx_sb_len", ctypes.c_ubyte),
        ("iovec_count", ctypes.c_ushort), ("dxfer_len", ctypes.c_uint),
        ("dxferp", ctypes.c_void_p), ("cmdp", ctypes.c_void_p),
        ("sbp", ctypes.c_void_p), ("timeout", ctypes.c_uint),
        ("flags", ctypes.c_uint), ("pack_id", ctypes.c_int),
        ("usr_ptr", ctypes.c_void_p), ("status", ctypes.c_ubyte),
        ("masked_status", ctypes.c_ubyte), ("msg_status", ctypes.c_ubyte),
        ("sb_len_wr", ctypes.c_ubyte), ("host_status", ctypes.c_ushort),
        ("driver_status", ctypes.c_ushort), ("resid", ctypes.c_int),
        ("duration", ctypes.c_uint), ("info", ctypes.c_uint),
    ]


class SGTransport:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR)

    def cmd(self, cdb, data_in=0, data_out=b"", timeout_ms=5000):
        cdb_buf = ctypes.create_string_buffer(bytes(cdb), len(cdb))
        sense = ctypes.create_string_buffer(32)
        if data_out:
            buf = ctypes.create_string_buffer(bytes(data_out), len(data_out))
            direction, ln = SG_DXFER_TO_DEV, len(data_out)
        elif data_in:
            buf = ctypes.create_string_buffer(data_in)
            direction, ln = SG_DXFER_FROM_DEV, data_in
        else:
            buf, direction, ln = None, SG_DXFER_NONE, 0
        h = sg_io_hdr(interface_id=ord("S"), dxfer_direction=direction,
                      cmd_len=len(cdb), mx_sb_len=32, dxfer_len=ln,
                      dxferp=ctypes.cast(buf, ctypes.c_void_p) if buf else None,
                      cmdp=ctypes.cast(cdb_buf, ctypes.c_void_p),
                      sbp=ctypes.cast(sense, ctypes.c_void_p), timeout=timeout_ms)
        fcntl.ioctl(self.fd, SG_IO, h)
        data = buf.raw[: ln - h.resid] if (buf and data_in) else b""
        return h.status, data, sense.raw[: h.sb_len_wr]


# ------------------------------------------------------------ USB BOT ---
class BOTTransport:
    def __init__(self, pid=None, iface=0):
        import usb.core
        import usb.util
        kw = {"idVendor": VID}
        if pid is not None:
            kw["idProduct"] = pid
        self.dev = usb.core.find(**kw)
        if self.dev is None:
            sys.exit("no 10d6 device found")
        try:
            if self.dev.is_kernel_driver_active(iface):
                self.dev.detach_kernel_driver(iface)
        except Exception as e:  # noqa
            print("detach:", e, file=sys.stderr)
        cfg = self.dev.get_active_configuration()
        intf = cfg[(iface, 0)]
        usb.util.claim_interface(self.dev, iface)
        self.ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        self.ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
        self.tag = 1

    def cmd(self, cdb, data_in=0, data_out=b"", timeout_ms=5000, lun=0, tag=None):
        ln = len(data_out) if data_out else data_in
        flags = 0x00 if data_out else 0x80
        if tag is not None:          # ADFU boot ROM wants tag 0 on every command
            self.tag = tag
        cbw = struct.pack("<IIIBBB", 0x43425355, self.tag, ln, flags, lun, len(cdb)) + bytes(cdb).ljust(16, b"\0")
        self.tag += 1
        self.ep_out.write(cbw, timeout_ms)
        data = b""
        if data_out:
            self.ep_out.write(data_out, timeout_ms)
        elif data_in:
            data = bytes(self.ep_in.read(data_in, timeout_ms))
        csw = bytes(self.ep_in.read(13, timeout_ms))
        sig, tag, resid, status = struct.unpack("<IIIB", csw)
        return status, data, b""


# ---------------------------------------------------------------- utils ---
def hexdump(b, base=0):
    for i in range(0, len(b), 16):
        chunk = b[i:i + 16]
        print(f"{base + i:08x}  {chunk.hex(' '):<47}  {''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)}")


def run(t, name, cdb, n):
    st, data, sense = t.cmd(cdb, data_in=n)
    print(f"== {name}  cdb={bytes(cdb).hex()} status={st} len={len(data)} sense={sense.hex()}")
    if data:
        hexdump(data)
    return data


def probe(t):
    run(t, "TEST UNIT READY", [0x00, 0, 0, 0, 0, 0], 0)
    run(t, "REQUEST SENSE", [0x03, 0, 0, 0, 18, 0], 18)
    run(t, "INQUIRY (255)", [0x12, 0, 0, 0, 0xFF, 0], 0xFF)
    vpd = run(t, "INQUIRY VPD 00", [0x12, 1, 0x00, 0, 0xFF, 0], 0xFF)
    if len(vpd) > 4:
        for p in vpd[4:4 + vpd[3]]:
            if p:
                run(t, f"INQUIRY VPD {p:02x}", [0x12, 1, p, 0, 0xFF, 0], 0xFF)
    run(t, "READ CAPACITY(10)", [0x25] + [0] * 9, 8)
    run(t, "READ FORMAT CAPACITIES", [0x23, 0, 0, 0, 0, 0, 0, 0, 0xFC, 0], 0xFC)
    run(t, "MODE SENSE(6) all", [0x1A, 0, 0x3F, 0, 0xC0, 0], 0xC0)


CHIP_IDS = {0x10D6: "ATJ2127 (MIPS32/MIPS16)", 0x3051: "ATJ2157 (ARMv7-M)"}


def cdb16(*head):
    return list(head) + [0] * (16 - len(head))


def actions_id(t):
    """MSC-mode vendor identify (read-only). Expect b'ACTIONSUSBD'."""
    c = cdb16(0xCC, 0, 0, 0, 0, 0, 0, 11)
    st, data, _ = t.cmd(c, data_in=11)
    print(f"CC identify: status={st} data={data!r}")
    return data == b"ACTIONSUSBD"


def adfu_reboot(t):
    """Ask the running firmware to reboot into the boot-ROM ADFU mode
    (re-enumerates as 10d6:10d6). Sequence from ilyakurdyukov/actions_flash."""
    if not actions_id(t):
        sys.exit("identify did not return ACTIONSUSBD; not sending reboot")
    c = cdb16(0xCB, 0x21, 0, 0, 0, 0, 0, 2)
    st, data, _ = t.cmd(c, data_in=2)
    print(f"CB 21 reboot-to-ADFU: status={st} data={data.hex()} (expect ff00)")


def adfu_info(t):
    """Boot-ROM ADFU identify (read-only): '\\0CADFUD' + 2-byte chip id."""
    c = cdb16(0xCC, 0, 0, 0, 0, 0, 0, 0x12)
    st, data, _ = t.cmd(c, data_in=0x12, tag=0)
    print(f"ADFU CC info: status={st}")
    hexdump(data)
    if data[:7] == b"\0CADFUD":
        cid = data[7] << 8 | data[8]
        print(f"chip id 0x{cid:04x} -> {CHIP_IDS.get(cid, 'unknown')}")


def adfu_cmd_cdb(sub, length, addr):
    return [0xCD] + list(struct.pack("<III", sub, length, addr)) + [0, 0, 0]


def adfu_readram(t, addr, size, out, step=0x200):
    """Boot-ROM READRAM (cd 93): read-only memory dump. Cannot read the ROM."""
    with open(out, "wb") as fo:
        done = 0
        while done < size:
            n = min(step, size - done)
            t.tag = 0
            cbw = struct.pack("<IIIBBB", 0x43425355, 0, n, 0x80, 0, 16) + bytes(adfu_cmd_cdb(0x93, n, addr + done))
            t.ep_out.write(cbw, 5000)
            buf = bytes(t.ep_in.read(n + 13, 5000))
            if len(buf) == n:  # CSW arrived separately
                buf += bytes(t.ep_in.read(13, 5000))
            data, csw = buf[:n], buf[n:n + 13]
            if len(csw) != 13 or struct.unpack_from("<I", csw)[0] != 0x53425355 or csw[12] != 0:
                print(f"bad CSW at 0x{addr + done:08x}: {csw.hex()}")
                break
            fo.write(data)
            done += n
    print(f"read 0x{done:x} bytes from 0x{addr:08x} -> {out}")


def _csw_ok(csw):
    return len(csw) == 13 and struct.unpack_from("<I", csw)[0] == 0x53425355 and csw[12] == 0


def adfu_xfer(t, sub, length, addr, data_out=None, data_in=0, timeout=5000):
    """One ADFU 'cd' command, tag 0. Returns data (for reads)."""
    flags = 0x80 if data_in else 0x00
    dlen = data_in if data_in else (len(data_out) if data_out else 0)
    cbw = struct.pack("<IIIBBB", 0x43425355, 0, dlen, flags, 0, 16) + bytes(adfu_cmd_cdb(sub, length, addr))
    t.ep_out.write(cbw, timeout)
    data = b""
    if data_out:
        t.ep_out.write(data_out, timeout)
    if data_in:
        buf = bytes(t.ep_in.read(data_in + 13, timeout))
        if len(buf) == data_in:
            buf += bytes(t.ep_in.read(13, timeout))
        data, csw = buf[:data_in], buf[data_in:]
    else:
        csw = bytes(t.ep_in.read(13, timeout))
    if not _csw_ok(csw):
        raise RuntimeError(f"ADFU cmd {sub:#x} @ {addr:#x}: bad CSW {csw.hex()}")
    return data


def adfu_writeram(t, addr, blob, step=0x200):
    for i in range(0, len(blob), step):
        chunk = blob[i:i + step]
        adfu_xfer(t, 0x13, len(chunk), addr + i, data_out=chunk)


def adfu_switch_load(t, path, addr=0x118000):
    """Load an ADFU server (actions_flash payload_arm/adfus.bin) and switch to it."""
    import time
    blob = open(path, "rb").read()
    adfu_writeram(t, addr, blob)
    back = adfu_xfer(t, 0x93, len(blob), addr, data_in=len(blob))
    if back != blob:
        sys.exit("verify of uploaded adfus failed; not switching")
    print(f"uploaded {len(blob)} bytes to {addr:#x}, verified")
    adfu_xfer(t, 0x20, 0, addr | 1)
    time.sleep(0.02)
    print("switched to adfus")


# Thumb byte-copy stub from actions_flash dump_mem2 (ATJ2157):
# copies *(0x11e020)=src, *(0x11e024)=n bytes to 0x11e028, returns ptr.
TINYCOPY_ARM = struct.pack("<16H", 0x4805, 0x0003, 0xC806, 0x6018, 0xF811, 0x3B01, 0x3A01,
                           0xF800, 0x3B01, 0xD1F9, 0x4800, 0x4770, 0xE020, 0x0011, 0, 0)[:28]
TC_CODE, TC_ARGS = 0x11E000, 0x11E020


def adfu_readmem2(t, addr, size, out):
    """Read any address (incl. mask ROM) via the tiny copy stub. Needs adfus."""
    step = 0x200 - 0x28
    adfu_writeram(t, TC_CODE, TINYCOPY_ARM)
    with open(out, "wb") as fo:
        done = 0
        while done < size:
            n = min(step, size - done)
            adfu_xfer(t, 0x13, 8, TC_ARGS, data_out=struct.pack("<II", addr + done, n))
            adfu_xfer(t, 0x21, 0, TC_CODE | 1)
            fo.write(adfu_xfer(t, 0x93, n, TC_ARGS + 8, data_in=n))
            done += n
    print(f"read {done:#x} bytes from {addr:#010x} -> {out}")


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--sg", help="/dev/sgN of the player")
    g.add_argument("--usb", action="store_true", help="raw libusb BOT")
    ap.add_argument("--pid", type=lambda s: int(s, 16))
    sub = ap.add_subparsers(dest="op", required=True)
    sub.add_parser("probe")
    sub.add_parser("actions-id")
    sub.add_parser("adfu-reboot")
    sub.add_parser("adfu-info")
    r = sub.add_parser("adfu-readram", help="boot-ROM read memory (read-only)")
    r.add_argument("addr", type=lambda s: int(s, 0))
    r.add_argument("size", type=lambda s: int(s, 0))
    r.add_argument("out")
    s = sub.add_parser("adfu-switch-load", help="upload adfus.bin to 0x118000 and switch")
    s.add_argument("file")
    r2 = sub.add_parser("adfu-readmem2", help="read any address via copy stub (needs adfus)")
    r2.add_argument("addr", type=lambda s: int(s, 0))
    r2.add_argument("size", type=lambda s: int(s, 0))
    r2.add_argument("out")
    c = sub.add_parser("cdb", help="send a raw CDB (hex) reading N bytes")
    c.add_argument("hex")
    c.add_argument("n", type=int, nargs="?", default=0)
    c.add_argument("-o", "--out")
    a = ap.parse_args()
    t = SGTransport(a.sg) if a.sg else BOTTransport(a.pid)
    if a.op == "probe":
        probe(t)
    elif a.op == "actions-id":
        actions_id(t)
    elif a.op == "adfu-reboot":
        adfu_reboot(t)
    elif a.op == "adfu-info":
        adfu_info(t)
    elif a.op == "adfu-readram":
        adfu_readram(t, a.addr, a.size, a.out)
    elif a.op == "adfu-switch-load":
        adfu_switch_load(t, a.file)
    elif a.op == "adfu-readmem2":
        adfu_readmem2(t, a.addr, a.size, a.out)
    else:
        data = run(t, "RAW", list(bytes.fromhex(a.hex)), a.n)
        if a.out:
            open(a.out, "wb").write(data)


if __name__ == "__main__":
    main()
