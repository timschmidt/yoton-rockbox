#!/usr/bin/env python3
"""Demand-paged link for the Rockbox yotonym03 (ATJ2157) main firmware.

  ym03_pagelink.py BUILDDIR analyze      size breakdown of the measure link
  ym03_pagelink.py BUILDDIR link         measure link -> page plan -> final link
                                         -> rockbox.ym03 (resident) + rockbox.pages

Pages are 8 KiB. Page p is linked at
    VA(p) = 0x200000 + (p >> 2) * 0x8000 + (p & 3) * 0x2000
which aliases SRAM2 slot (p & 3); the pager (pager-atj2157.c) maps pages on
MemManage faults by moving the MPU allow-region of that slot. Code and rodata
pages share the same window and slots.
"""
import os, re, struct, subprocess, sys, collections

PAGE = 0x2000
NSLOT = 4
MAX_PAGES = 128
RES_BASE = 0x100000
RES_LEN = 0x38000          # SRAM0

# Objects whose code/rodata stay resident (hot paths, pager dependencies)
RESIDENT_OBJ = re.compile(
    r"(libgcc\.a|libc\.a|libm\.a|crt0|vectors)"
    # firmware core: system, pager, kernel, IRQ-time drivers, panic
    r"|libfirmware\.a\(("
    r"system-\w+|pager-\w+|timer-\w+|irqhandlers\w*|vectors\w*|crt0\w*"
    r"|kernel|thread[\w-]*|corelock|mutex|semaphore|queue|tick|mrsw_lock|mrsw-lock|kernel-\w+"
    r"|linked_list|events|sysfont|usb-atj2157"
    # real-time storage path: file system and SD reads for the buffering thread
    r"|fat|file|file_internal|fileobj_mgr|disk_cache|disk|sdmmc_host|storage|led"
    r")\.o\)"
    # DSP core: runs for every PCM insert
    r"|librbcodec\.a\(("
    r"dsp_core|dsp_sample_input|dsp_sample_output|dsp_misc|channel_mode|pga"
    r")\.o\)"
    r"|libfirmware\.a\(("
    r"lcd-yotonym03|backlight-yotonym03|button-yotonym03|power-yotonym03|pcm-yotonym03"
    r"|sdmmc-yotonym03|misc-yotonym03|panic|memcpy\w*|memset\w*|memmove\w*|strlen|"
    r"strcmp|strncmp|strlcpy|strcpy|strchr|strrchr|strcasecmp|ffs|crc32|button|backlight|powermgmt|pcm|pcm_mixer"
    r"|pcm_sw_volume|pcm_sampr|debug|logf|general|usb|timeout"
    r")\.o\)"
    # PCM buffer callbacks run from the DMA interrupt
    r"|/apps/(pcmbuf|voice_thread|buffering|codec_thread|codecs)\.o")


# Individual functions that run from interrupt context (tick tasks) or are
# otherwise hot enough that paging them would thrash.
RESIDENT_SEC = re.compile(
    r"^\.text\.(car_adapter_tick|pcmbuf_fade_tick|dbg_audio_task"
    # playback engine callbacks on the per-chunk audio path (profiled)
    r"|audio_codec_update_offset|audio_pcmbuf_may_play|audio_pcmbuf_position_callback"
    r"|audio_pcmbuf_track_change|audio_pcmbuf_sync_position|buffer_event_\w+"
    r"|id3_mutex_\w+|audio_current_track|audio_status|playback_status"
    r"|playlist_peek|get_next_index\S*|codec_\w+_callback"
    r"|calculate_step_count|audio_fill_file_buffer|scroll_thread"
    # built-in MP3 decoder's per-frame glue (profiled: it evicted the
    # decoder's pages every frame)
    r"|builtin_mp3_run|audio_codec_update_elapsed|adjust_mp3entry|hdr_compare"
    # host test-mode tick hook (runs every tick)
    r"|ym03_wps_profile_tick)$")


def va(p):
    """code page p: SRAM2 window, 4 slots"""
    return 0x200000 + (p >> 2) * 0x8000 + (p & 3) * PAGE


def va_ro(q):
    """rodata page q: SRAM3 window above 0x340000 (below it SRAM3 is RAM,
    see pager-atj2157.c), 2 slots"""
    assert q < 48, "rodata page store full"
    return 0x340000 + (q >> 1) * 0x8000 + (q & 1) * PAGE


def parse_map(path):
    """Return list of (outsec, insec, addr, size, file) for every input section."""
    out = []
    outsec = None
    pending = None
    for line in open(path):
        m = re.match(r"^(\.\S+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+", line)
        if m and not line.startswith(" "):
            outsec = m.group(1)
            continue
        if not line.startswith(" ") and line.strip():
            if re.match(r"^\.\S+\s*$", line):
                outsec = line.strip()
            continue
        m = re.match(r"^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m:
            out.append((outsec, m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4).strip()))
            pending = None
            continue
        m = re.match(r"^ (\.\S+)\s*$", line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m and pending:
            out.append((outsec, pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3).strip()))
            pending = None
    return out


def kind(insec):
    if insec.startswith((".text", ".icode", ".init")):
        return "text"
    if insec.startswith((".rodata", ".irodata")):
        return "rodata"
    if insec.startswith((".data", ".idata")):
        return "data"
    if insec.startswith((".bss", ".ibss", "COMMON")):
        return "bss"
    return "other"


# Small tables read in inner loops together with other rodata: paged, they
# share a slot with the data they are compared against and every access
# faults twice (profiled: tolower() in strcasecmp() over setting names and
# file extensions, ~20000 faults while loading settings).
RESIDENT_RO = re.compile(r"^\.rodata\.(_ctype_)$")


def pageable(sec):
    outsec, insec, addr, size, f = sec
    if outsec != ".text" or size == 0:
        return False
    if kind(insec) not in ("text", "rodata"):
        return False
    if insec.startswith((".icode", ".irodata", ".init", ".vectors")):
        return False
    if RESIDENT_OBJ.search(f):
        return False
    if RESIDENT_SEC.match(insec) or RESIDENT_RO.match(insec):
        return False
    if kind(insec) == "text" and size > PAGE - 64:
        return False                      # code larger than a page stays resident
    if kind(insec) == "rodata" and size > 2 * PAGE - 64:
        return False                      # rodata may span at most 2 pages
    return True


def analyze(build):
    secs = parse_map(os.path.join(build, "rockbox.map"))
    tot = collections.Counter()
    for s in secs:
        k = kind(s[1])
        tag = "paged" if pageable(s) else "resident"
        tot[(s[0], k, tag)] += s[3]
    for key, v in sorted(tot.items(), key=lambda kv: tuple(str(x) for x in kv[0])):
        if v:
            if key[0] in (".text", ".bss", ".framebuffer"): print(f"{key[0]:12} {key[1]:7} {key[2]:9} {v / 1024:8.1f} KB")
    res = sum(v for (o, k, t), v in tot.items() if t == "resident" and o == ".text")
    pag = sum(v for (o, k, t), v in tot.items() if t == "paged")
    bss = sum(v for (o, k, t), v in tot.items() if o == ".bss")
    print(f"resident image {res / 1024:.1f} KB, paged {pag / 1024:.1f} KB "
          f"(~{pag / PAGE:.0f} pages), bss {bss / 1024:.1f} KB")
    big = collections.Counter()
    for s in secs:
        if s[0] == ".bss":
            big[s[4].split("/")[-1]] += s[3]
    print("largest bss:", ", ".join(f"{f}={v // 1024}K" for f, v in big.most_common(10)))
    return secs


def lds_file_pattern(f):
    m = re.match(r"^(.*/)?([^/]+\.a)\(([^)]+)\)$", f)
    if m:
        return f"*{m.group(2)}:{m.group(3)}"
    return f


def pack(lst, allow_span=False):
    """First-fit in order. With allow_span, a section bigger than a page is
    placed at the start of an even page and spills into the next one (the
    two pages are VA-contiguous and use different rodata slots)."""
    pages, used = [[]], [0]
    for s in lst:
        sz = (s[3] + 7) & ~7
        if sz > PAGE:
            if not allow_span:
                continue
            # open a fresh even-numbered page pair
            if used[-1]:
                pages.append([]); used.append(0)
            if (len(pages) - 1) & 1:
                pages.append([]); used.append(0)
            pages[-1].append(s)
            used[-1] = PAGE
            pages.append([("span",)])     # marker: continuation page
            used.append(sz - PAGE)
            continue
        if used[-1] + sz > PAGE:
            pages.append([])
            used.append(0)
        pages[-1].append(s)
        used[-1] += sz
    return pages, used


def load_hot(build, name="ym03_hot.txt"):
    """ym03_hot.txt (from ym03_prof.py --add): 'count file-pattern section'
    lines, hottest first."""
    path = os.path.join(build, name)
    if not os.path.exists(path):
        # the profiled list kept with the tools (paths relative to a build)
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
    order = {}
    if os.path.exists(path):
        base = os.path.abspath(build) + "/"
        for i, l in enumerate(open(path)):
            v, f, sec = l.split()
            # "$BUILD/" stands for this build directory (objects outside archives)
            order[(f.replace("$BUILD/", base), sec)] = i
    return order


HOT = {}
# Second profiled group: the file browser's list drawing (ym03_hot_ui.txt).
# It runs while the hot pages (Now Playing path, also the font and LCD
# helpers) stay mapped, so its pages go only where they map to code slots 2
# and 3 (see plan_pages).
HOT_UI = {}


def hot_first(lst):
    """Profiled hot sections first (hottest first), then the rest in link
    order, so the pages the audio path and UI keep touching are dense."""
    hot = [s for s in lst if (lds_file_pattern(s[4]), s[1]) in HOT]
    hot.sort(key=lambda s: HOT[(lds_file_pattern(s[4]), s[1])])
    cold = [s for s in lst if (lds_file_pattern(s[4]), s[1]) not in HOT]
    return hot + cold


def plan_pages(secs):
    """Pack pageable sections into 8K pages (code and rodata separately),
    hot sections first, the rest keeping each object's sections together."""
    code = hot_first([s for s in secs if pageable(s) and kind(s[1]) == "text"])
    ro = hot_first([s for s in secs if pageable(s) and kind(s[1]) == "rodata"])
    ui_key = lambda s: (lds_file_pattern(s[4]), s[1])
    ui = [s for s in code if ui_key(s) in HOT_UI and ui_key(s) not in HOT]
    ui.sort(key=lambda s: HOT_UI[ui_key(s)])
    code = [s for s in code if s not in ui]
    nhot = sum(1 for s in code if ui_key(s) in HOT)
    hp, hu = pack(code[:nhot])
    cp, cu = pack(code[nhot:])
    up, uu = pack(ui) if ui else ([], [])
    # hot pages first, then cold ones, with the UI group's pages placed at
    # page numbers that map to slots 2 and 3
    pages, used = list(hp), list(hu)
    ci = ui_i = 0
    while ci < len(cp) or ui_i < len(up):
        if ui_i < len(up) and (len(pages) & 3) >= 2:
            pages.append(up[ui_i]); used.append(uu[ui_i]); ui_i += 1
        elif ci < len(cp):
            pages.append(cp[ci]); used.append(cu[ci]); ci += 1
        else:
            pages.append([]); used.append(0)        # pad to a slot 2/3 page
    cp, cu = pages, used
    rp, ru = pack(ro, allow_span=True)
    if len(cp) > MAX_PAGES or len(rp) > 48:
        sys.exit(f"too many pages: code {len(cp)}, rodata {len(rp)}")
    return cp, cu, rp, ru


def write_pages_lds(build, cpages, rpages):
    """One output section per page. A spanning section's output section
    covers two pages; its continuation page has no section of its own."""
    with open(os.path.join(build, "ym03_pages.lds"), "w") as fo:
        fo.write("/* generated by ym03_pagelink.py */\n")
        for name, pages, vfn in (("pg", cpages, va), ("pr", rpages, va_ro)):
            for p, lst in enumerate(pages):
                if lst and lst[0] == ("span",):
                    continue
                fo.write(f"    .{name}_{p} {vfn(p):#x} (COPY) : {{\n")
                for s in lst:
                    fo.write(f"        {lds_file_pattern(s[4])}({s[1]})\n")
                fo.write("    }\n")


def run_make(build):
    env = dict(os.environ)
    env["PATH"] = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xbin") + ":" + env["PATH"]
    for f in ("rockbox.elf", "rockbox.map"):
        try:
            os.unlink(os.path.join(build, f))
        except FileNotFoundError:
            pass
    r = subprocess.run(["make", "-j16", os.path.join(os.path.abspath(build), "rockbox.elf")],
                       cwd=build, env=env, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit("make failed")


def link(build):
    with open(os.path.join(build, "ym03_layout.lds"), "w") as fo:
        fo.write("YM03_SRAM0_ORG = 0x10000000;\nYM03_SRAM0_LEN = 0x1000000;\n")
    open(os.path.join(build, "ym03_pages.lds"), "w").close()
    run_make(build)
    HOT.update(load_hot(build))
    if HOT:
        print(f"{len(HOT)} profiled hot sections packed first")
    HOT_UI.update(load_hot(build, "ym03_hot_ui.txt"))
    if HOT_UI:
        print(f"{len(HOT_UI)} profiled UI sections in slots 2-3")
    secs = analyze(build)
    cp, cu, rp, ru = plan_pages(secs)
    print(f"code pages {len(cp)} ({sum(cu) * 100 // max(1, len(cp) * PAGE)}% full), "
          f"rodata pages {len(rp)} ({sum(ru) * 100 // max(1, len(rp) * PAGE)}% full)")
    write_pages_lds(build, cp, rp)
    with open(os.path.join(build, "ym03_layout.lds"), "w") as fo:
        fo.write(f"YM03_SRAM0_ORG = {RES_BASE:#x};\nYM03_SRAM0_LEN = {RES_LEN:#x};\n")
    run_make(build)
    elf = os.path.join(build, "rockbox.elf")
    oc = "arm-none-eabi-objcopy"
    subprocess.run([oc, "-O", "binary", "-R", ".pg_*", "-R", ".pr_*", elf,
                    os.path.join(build, "rockbox.ym03")], check=True)
    n = len(cp) + len(rp)
    store = bytearray(b"\xff" * (n * PAGE))
    tmp = os.path.join(build, "pg.tmp")
    names = [(f".pg_{p}", cp[p]) for p in range(len(cp))] + [(f".pr_{q}", rp[q]) for q in range(len(rp))]
    for i, (sec, lst) in enumerate(names):
        if lst and lst[0] == ("span",):
            continue                      # filled by the previous page's section
        subprocess.run([oc, "-O", "binary", "--only-section", sec,
                        "--set-section-flags", f"{sec}=alloc,load,contents", elf, tmp], check=True)
        data = open(tmp, "rb").read()
        if len(data) > 2 * PAGE or (len(data) > PAGE and not (i + 1 < len(names) and names[i + 1][1][:1] == [("span",)])):
            sys.exit(f"{sec} too big ({len(data)})")
        store[i * PAGE: i * PAGE + len(data)] = data
    os.unlink(tmp)
    hdr = struct.pack("<4sIII", b"YMPG", len(cp), len(rp), sum(store) & 0xffffffff)
    open(os.path.join(build, "rockbox.pages"), "wb").write(hdr.ljust(PAGE, b"\xff") + store)
    print("resident image:", os.path.getsize(os.path.join(build, "rockbox.ym03")), "bytes;",
          f"page store: {(n + 1) * PAGE // 1024} KB")


if __name__ == "__main__":
    build, op = sys.argv[1], sys.argv[2]
    if op == "analyze":
        analyze(build)
    elif op == "link":
        link(build)
