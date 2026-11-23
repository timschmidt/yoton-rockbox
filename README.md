# Rockbox on the Yoton YM03

A port of [Rockbox](https://www.rockbox.org) to the Yoton YM03 MP3 player
(Actions ATJ2157: Cortex-M4 at 48 MHz, 320 KiB SRAM, 8 MiB SPI NOR, microSD,
128×128 LCD), plus the tools used to bring it up over USB.

Status, hardware notes and design: [FINDINGS.md](FINDINGS.md) (section 13 is the
current state of the port).

## Layout

* `patches/` – the Rockbox port as a patch against upstream Rockbox
  (`git am` it onto `https://github.com/Rockbox/rockbox`, branch point noted in
  the patch).
* `yoton.py` – USB access to the player: boot-ROM recovery mode (ADFU), a small
  RAM monitor (ymon), NOR read/write, payload execution.
* `payloads/` – bare-metal code run from RAM or installed to NOR: the two-stage
  boot stub (`ym03boot1.c`, `ym03stub.c`), the NOR programmer, probes.
* `tools/ym03_pagelink.py` – links the demand-paged firmware (resident image +
  page store) using the profiled hot-section list `tools/ym03_hot.txt`.
* `tools/ym03_install.py` – stages Rockbox to NOR and installs/restores the boot
  stub (`stage`, `flash-stub --i-have-a-backup`, `restore-vendor`).
* `tools/ym03_maintest.py` – boots a build from RAM in test mode and reads the
  results back (playback timing, UI key injection, fault/CPU profiles, …).

## Building

Toolchain: `arm-none-eabi-gcc` (the `tools/xbin` links provide the
`arm-elf-eabi-*` names Rockbox's build expects).

    git clone https://github.com/Rockbox/rockbox ext/rockbox
    git -C ext/rockbox am ../../patches/0001-*.patch
    mkdir -p build/ym03-main && cd build/ym03-main
    ../../ext/rockbox/tools/configure --target=yotonym03 --type=N
    cd ../.. && python3 tools/ym03_pagelink.py build/ym03-main link
    make -C payloads stub        # NOR boot stub, stages 1 and 2

## Installing

Keep a full NOR backup first (`yoton.py` can dump it). With the player in ADFU
mode:

    python3 tools/ym03_install.py build/ym03-main stage
    python3 tools/ym03_install.py build/ym03-main flash-stub --i-have-a-backup

At power-up: no key → Rockbox, Select → stock firmware, Right → USB recovery.
Copy `.rockbox/` (codecs, plugins) to the card's root.
