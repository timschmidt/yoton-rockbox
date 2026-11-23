# Yoton MP3 player — device investigation notes

Owner's own device, investigated to load fixed / updated firmware.
Host: Fedora 43, kernel 7.2.11. All USB traffic below was read-only unless
marked otherwise. Raw captures: `probe_msc.txt`, `probe_adfu.txt`,
`adfu_descriptors.txt`.

## 1. Product (from vendor material)

| Item | Value | Source |
|---|---|---|
| Models | YM03 (Bluetooth 5.0 sport, clip), YM04, YM05 (BT 4.1) | yoton.co/pages/mp3-player, FAQ page |
| Storage | **No internal storage**; microSD slot up to 128 GB (ships with 64 GB card) | YM03 manual |
| Battery | 370 mAh | manuals |
| Formats | MP3/WMA/APE/FLAC/WAV/"DRM"/"ACELP"; JPG/BMP; rec. WAV/MP3; LRC; **AMV video** (YM04/YM05); TXT (YM04) | manuals |
| Output | 10 mW@32 Ω / 20 mW@16 Ω | manual |
| Firmware update | Vendor PC tool ("Update Firmware" button); firmware supplied by customer service on request, not published | manuals |
| Reset | Pinhole next to the earphone jack | manual FAQ |

AMV video and ACELP voice recording are hallmarks of Actions Semiconductor SoCs.

## 2. USB — normal (player / MSC) mode

```
VID:PID        10d6:1101  (Actions Semiconductor)
bcdUSB 2.00, bcdDevice 1.00, High Speed, MaxPower 500 mA, 1 config, 1 interface
iManufacturer  "ACTIONS"
iProduct       "HS USB FlashDisk"
iSerial        "CAcdefghijklm..."   (SDK placeholder, not unique)
Interface      class 08 (Mass Storage), subclass 05 (SFF-8070i), protocol 50 (BOT)
Endpoints      0x81 bulk IN 512, 0x02 bulk OUT 512
```

These strings/IDs are the **stock defaults of the Actions MP3 SDK**
(`case/fwpkg/config.txt`: `USB_VID 0x10D6`, `USB_PID 0x1101`,
`"ACTIONS"`, `"HS USB FlashDisk"`, `"ACTIONS USB DISK FOB 2.00"`).

Behaviour: on the first connect (04:52) the LUN reported *Medium not present*
(likely charge-only mode / no selection on the player); on the second
connect (05:07) the card was exposed normally.

### SCSI responses

| Command | Result |
|---|---|
| TEST UNIT READY | GOOD |
| REQUEST SENSE | `70 00 00 … 0a …` (no sense) |
| INQUIRY | PDT 0 (direct access), RMB=1, version 0, response fmt 1, add. len 0x1f; vendor `ACTIONS `, product `USB DISK FOB 2.0`, rev `0` |
| READ CAPACITY(10) | last LBA 0x1DC97FFF, block 512 → 499 744 768 blocks = 255.87 GB (238.3 GiB) — the inserted 256 GB microSD |
| READ FORMAT CAPACITIES | one descriptor, 0x1DC98000 blocks × 512, formatted media |
| MODE SENSE(6) page 3F | pages 01 (R/W error recovery), 08 (caching), 1B (removable block access), 1C |
| VPD pages | not implemented — any EVPD request returns the standard INQUIRY buffer |

**Firmware quirk — allocation length is not honoured.** INQUIRY and MODE
SENSE return as many bytes as the host asks for, straight from the firmware's
static response tables. What that exposes:

* a second INQUIRY template: PDT 5 (CD-ROM), RMB=1, `ACTIONS USB DISK CDROM 1.00`
  → the firmware has a built-in (here disabled) virtual CD-ROM LUN, the usual
  Actions "autorun PC tool" feature;
* per-LUN capacity / format-capacity templates (several LUN slots);
* the firmware's sense-data table (fixed-format `70 00 SK … 0a … ASC`):
  SK0/ASC00 no sense, SK0B/ASC4E overlapped cmds, SK07/ASC27 write-protected,
  SK06/ASC28 medium changed, SK02/ASC3A medium not present, SK05/ASC20 invalid opcode.

### Card contents
Single FAT (vfat) partition labelled `MUSIC`, 239 GB, 97 GB used, only the
owner's music folders. **No player system files on the card** → firmware,
settings and resources live in on-board flash (expected: SPI NOR).

### Actions vendor commands (MSC mode)
| CDB (16 bytes) | Data-in | Response | Meaning |
|---|---|---|---|
| `cc 00 00 00 00 00 00 0b 00…` | 11 | `ACTIONSUSBD` | vendor identify — confirms Actions SDK firmware |
| `cb 21 00 00 00 00 00 02 00…` | 2 | (device re-enumerates before CSW) | **reboot into boot-ROM ADFU** — *state-changing*, no flash write |

Sequence taken from `ilyakurdyukov/actions_flash` (`adfu_reboot`). Works on
this unit: the player dropped off the bus 2 s later and returned as ADFU.

## 3. USB — ADFU (boot-ROM recovery) mode

```
VID:PID        10d6:10d6
bcdUSB 2.00, bcdDevice 1.00, High Speed, MaxPower 300 mA
No string descriptors (iManufacturer/iProduct/iSerial = 0)
Interface      class FF / subclass FF / protocol FF (vendor)
Endpoints      0x81 bulk IN 512, 0x02 bulk OUT 512
```

The ADFU protocol is CBW/CSW-framed (Bulk-Only style) with CBW tag 0 on every
command. Documented commands (actions_flash):

| CDB | Purpose |
|---|---|
| `cc …` len 0x12 | identify: `"\0CADFUD"` + 2-byte chip id (`10 d6` = ATJ2127, `30 51` = ATJ2157) |
| `cd` + sub-cmd LE32 + len LE32 + addr LE32 | 0x13 WRITERAM, 0x93 READRAM, 0x20 SWITCH, 0x21 EXEC, 0x22 RETSIZE, 0x23 READRET, 0x10 / 0x60 FLASH |
| `b0` | reset (once the vendor ADFU server `adfus` is loaded) |

In ADFU mode the device is vendor-class and gets no logged-in-user ACL, so
`99-actions-yoton.rules` must be installed (done 05:14).

### ADFU identify (read-only)
```
cc 00 00 00 00 00 00 12 …  →  00 43 41 44 46 55 44 30 51 41 00 00 00 00 00 00 00 00
                              "\0CADFUD"  chip id 0x3051  "A"
```
**Chip id 0x3051 = ATJ2157** (ARMv7-M, Thumb-2; Cortex-M4F class).

### Memory read via boot ROM (`cd 93` READRAM, read-only)
* SRAM **0x00100000–0x00137FFF (224 KB)**, all readable; ~0.1 s for the
  whole range. Saved: `dumps/sram_100000_224k.bin`.
* Addresses 0x0–0xFFFF return the SRAM contents (4 bytes differ: live USB
  counters). READRAM cannot read the mask ROM itself, as actions_flash
  documents. Dumping the ROM takes a RAM payload (`adfus` + `read_mem2 0 64K`).
* Boot-ROM vector table at SRAM base (ROM handlers in the 64 KB ROM at 0x0):

  | Vector | Value | | Vector | Value |
  |---|---|---|---|---|
  | initial SP | 0x00101000 | | HardFault | 0x00001A7D |
  | Reset | 0x00002521 | | MemManage | 0x00001C69 |
  | NMI | 0x000014DD | | BusFault | 0x00001295 |
  | UsageFault | 0x000048C5 | | | |

### Firmware residue left in SRAM (warm reboot from the player OS)
The ADFU reboot does not clear SRAM, so the dump shows the running firmware:

* **Storage:** `nor_ud.drv` (USB-disk driver for NOR), `norid:`,
  `adjust spi clk err!`, `CMU_SPI0CLK:`, `DEVPLL_CTL:`, `use HOSC!`
  → **firmware boots from SPI NOR on SPI0.**
* **Boot / security:** `KER_TEXTBIN`, `KER_DATABIN`, `KER_INITBIN`,
  `decrypt fail!`, `begin sys encryp!`, `No efuse file!`, `read ictype fail!`,
  `read blk0 fail!` / `read blk1 fail!`, `welcome adfu!`, `welcome s4!`
  → the kernel is loaded in segments and **decrypted** at boot. eFuse and
  IC-type checks exist, and two redundant boot blocks.
* **Modules (SDK US215A-style):** `kernel.drv`, `manager.ap`, `card.drv`,
  `cards.drv`, `nand.drv`, `uhost.drv`, `nor_ud.drv`, `fat16s.drv`,
  `fat32n.drv`, `exfatn.drv`, `exfats.drv`, `uk.bin`, `m_icon.bin`;
  UI themes `mainmenu.sty`, `common.sty`, `browser.sty`/`.mcg`,
  `setting.sty`, `udisk.sty`, colour variants `cm_{pink,red,green,gray}.sty`,
  `set_{gray,pink,red}.sty`, `setgreen.sty`, `ray.sty`.
* **OS:** banked code (`ap_bank epc:`, `kernel_bank epc:`), RTOS task/message
  queues (`task stk err!`, `mq_send err line:`), Cortex-M fault dump
  (`ICSR_val:`, `fault_handler_sp/lr:`), RTC alarm, clock scaling
  (`adjust_clk fail`, `freq_level:`, `devpll fail1/2`).
* **USB MSC stack:** `ACTIONS USB DISK FOB 2.00`, `ACTIONS USB DISK CDROM  1.00`,
  `ACTIONSUSBD`, `CUSBD`, `cbw len err`, `NotSupportCmd`, `csw_report`,
  `full speed`/`high speed`.
* **Serial number:** `sn.txt`, `acts_SN_err`.
* Debug-print strings (`welcome adfu!`, `nand debug addr:`, `WAKE_PD:`,
  `DC5V_ON!`) suggest a **UART debug console** on the board (pads not yet located).
* Unexplained string: `Changhua` (twice, next to `data:`/`para:`), possibly
  the ODM/customer tag.

**Leaving ADFU:** unplug and press the reset pinhole (next to the earphone jack).
The player has a soft power switch, so without a reset it can stay in ADFU
until the battery runs down.

## 4. SoC — current assessment

* Vendor: **Actions Semiconductor (Zhuhai)**. Confirmed by USB IDs, the
  `ACTIONSUSBD` vendor reply and a working ADFU entry.
* **SoC: Actions ATJ2157 (confirmed by boot-ROM chip id 0x3051).** ARMv7-M
  Thumb-2 core (reported as Cortex-M4F, ~288 MHz). 224 KB SRAM at 0x00100000,
  64 KB mask ROM at 0x0. Shares peripheral IP blocks with ATJ2127 at different
  addresses.
* **Firmware storage: SPI NOR on SPI0** (confirmed by `nor_ud.drv` and the
  SPI0 clock strings in RAM). The kernel image is encrypted on flash.
* Boot chain (by analogy with ATJ2127, partly confirmed by strings): mask ROM
  → boot blocks (blk0/blk1) on NOR → decrypt + load `KER_*BIN` → `kernel.drv`
  → `manager.ap` → apps (`*.ap`), drivers (`*.drv`), resources (`*.sty`).

## 5. Firmware formats and tools (for the next steps)

* Vendor `.fw` update = encrypted SQLite3 DB (FileTable: ADFUS.BIN, fwsc*,
  mbrec, brec, FWIMAGE.FW, *.ap, *.drv). Older `UPGRADE.HEX` = FWU → AFI.
* On-device image: LFI / `FWIMAGE.FW` (magic 0x0FF0AA55, 512-byte header,
  240×32-byte directory, 32-bit word-sum checksums).
* Tools in `ext/`: `actions_flash` (actions_dump, fwhelper: scan/unpack/repair
  LFI, ARM and MIPS payloads), Rockbox `utils/atj2137` (atjboottool FWU/AFI/LFI
  pack/unpack incl. `--atj2127`, adfuload, sdboot — written for ATJ213x).
* Prior art: RUIZU X02 (ATJ2127) custom code by patching `.ap` files in the
  decrypted update DB (gitlab.com/reverse2682701/ruizu-x02-rev).

## 4b. Code execution in SRAM (done 05:18–05:19, owner-approved)

* `adfus.bin` (actions_flash `payload_arm/adfus.c`, read before building) was
  built with Fedora `arm-none-eabi-gcc 15.2.0` (`-Os -march=armv7-m -mthumb`,
  IMAGE_START 0x118000), 1000 bytes. It was uploaded with `cd 13`, verified by
  read-back, and started with `cd 20` SWITCH. The device stayed on the bus as
  10d6:10d6 and kept answering.
* With the 28-byte Thumb copy stub at 0x11e000 (`adfu-readmem2`), the first
  512 bytes of the **mask ROM** were read (`dumps/rom_test.bin`). The ROM vector
  table is initial SP 0x00101B18, Reset 0x00000189, NMI 0x0000019F,
  HardFault 0x000001A1. It differs from the SRAM alias, which confirms it is the real ROM.
* The full 64 KB ROM dump was **not completed**. The run was interrupted and no
  file was written.

## 5b. Firmware flashing procedure (how the vendor tool does it)

1. Player running normally (`10d6:1101`, MSC). The PC tool sends
   `cc …0b` (expects `ACTIONSUSBD`), then `cb 21 …02`, and the player reboots into ADFU.
   *Fallback if the firmware is broken:* hold the boot key while plugging
   in with the player off, or let the mask ROM fall back to ADFU when NOR does
   not boot.
2. ADFU `10d6:10d6`: `cd 13` WRITERAM of the `adfus` server to
   **0x118000** (ATJ2157), then `cd 20` SWITCH to it.
3. Load and `cd 21` EXEC a flash-hardware script (`fwsc*`, from the `.fw`
   package) at **0x11e000**. It initialises the SPI NOR and reports its geometry.
4. `cd 10` FLASH writes: mbrec, then brec, then FWIMAGE (LFI), then the
   flash_end marker (ATJ2127 sector tags 0x40000000 / 0x46000000 / 0xc0000000
   / 0xff000000; ATJ2157 is expected to be the same).
5. `b0` → reset, and the player boots the new firmware.

Steps 1–2 and the read side (`cd 93`) are verified on this unit. Steps 3–5 come
from usbmon captures by the actions_flash author (ATJ2127) and still have to be
recorded for ATJ2157. Capture the official Yoton updater with usbmon once
support provides a `.fw`.

## 6. Next steps

1. ~~Install the udev rule, then run `adfu-info` → chip id~~ (done: ATJ2157).
2. Run code in SRAM: build actions_flash `payload_arm/adfus.c` (ATJ2157 ADFU
   server, load at 0x118000; payloads at 0x11e000) with `arm-none-eabi-gcc`
   (installed), then `read_mem2 0 64K` → mask ROM dump.
3. SPI NOR dump: actions_flash has no NOR reader for ATJ2157 yet. Write a
   small Thumb payload that drives SPI0 (JEDEC 0x9F id, then 0x03 reads). The
   SPI0 register layout can be recovered from `nor_ud.drv` code in the SRAM
   residue or from the ROM dump. Back the image up before writing anything.
   Alternatively, read the NOR chip with a SOIC-8 clip and a CH341A/flashrom.
3. Get a Yoton `.fw` update from support (or a matching AGPTEK/RUIZU one for
   the same chip), then decrypt and unpack it with fwhelper/atjboottool.
4. Open the case: SoC marking, SPI NOR part number, test pads (UART / SWD on an
   ARM part). A clip-on SPI programmer gives an independent recovery path.
5. Only then: patch or rebuild FWIMAGE.FW and flash it via ADFU.

---

# Part 2 — 2026-11-22 (Rockbox port session)

## 7. Memory map (measured with fault-safe probes, `payloads/memprobe*.c`, `ram*.c`)
| Region | Size | Notes |
|---|---|---|
| 0x00000000 | 64 KB | mask ROM (CPU view); DMA sees SRAM0 alias here |
| 0x00100000–0x00137FFF | 224 KB | SRAM0 (ROM uses 0x100000–0x101B18 during boot) |
| 0x00158000–0x0015FFFF | 32 KB | SRAM1 |
| 0x00200000 (+32 KB, aliased over 1 MB) | 32 KB | SRAM2; **DMA address 0x10000000** |
| 0x00300000 (+32 KB, aliased over 1 MB) | 32 KB | SRAM3; **DMA address 0x18000000** |
Total RAM **320 KB**. No external RAM; unmapped reads return 0xE800E800 (code bus) or 0 (system bus), never fault.
CPU: Cortex-M4 r0p1 (CPUID 0x410FC241), no cache. DWT CYCCNT appears not to count.

## 8. SPI NOR
JEDEC **5E 40 17** (Zbit, 8 MB). SPI0 @0xC00A0000 (CTL/STAT/TX/RX/BC), ROM pin group 0 (MFP regs 0x34–0x40, func 7).
Backups: `dumps/nor_backup_8m.bin` (raw, sha256 ad45687e…), `dumps/nor_descrambled_8m.bin` (via ROM DMA read with
the SPI hardware descrambler, CTL bit 12). The descrambled image is plaintext: mbrec @0 (0xA5 = 512 B, loaded to 0x101000,
entry 0x101011, checksum = Σhalfwords[1..]+0x1234), LFI @0xF400 ("1.101.5", 20140616), directories @0xF000/0x10000.
All 96 files extracted to `re/fw/` with checksums verified. Free NOR: 0x770000–0x7DFFFF (448 KB of 0xFF).

## 9. Board (CONFIG.TXT + vendor drivers, verified where marked ✔)
* ✔ LCD: LCM 8080 bus @0xC01A0000, ST7735S-class **128×128 RGB565**, init table in DRV_LCD bank 2, panel offset x+2/y+3 (MADCTL C8).
  D0–7=GPIO0–7, RS=8, RD=10, WR=11 (func 2), CS=43, RST=21, BL=22 (PWM4 @0xC0190400).
* ✔ microSD: SD0 @0xC0160000, 4-bit, pins 27–33 (func 0x804), DMA ch4 mode 0x85; MBR of the 256 GB card read OK.
* Keys: LRADC1 = PMU+0x50 (idle 0x2E7), GPIO24/25; per-key ADC values **not yet measured** (needs key presses).
* Battery ADC PMU+0x3C (≈0x37C). Power-off: PMU+0x20 bits 1 then 0.
* Pin cfg 0xC01C0004+4·pin; GPIO out-en 0x200, set 0x210, clr 0x220, in 0x230. Clocks CMU 0xC0001000, resets 0xC0000000.

## 10. Boot ROM flow (0x2488)
clock init → flags = backup word 3 (block 0xC0020000, unlock key 0x004B4559) → **flag 0x08: ADFU immediately** →
parallel NAND boot → SPI NOR/NAND boot (0x1B14; tries 0x0/0x1000/0x10000, descrambled then raw) → SD boot ×2 →
flag 0x04 / fallthrough: ADFU. **No boot-key check in the ROM**; the persistent flag is why resets return to ADFU.
The boot record has only a 16-bit checksum (no signature).

## 11. Tooling
* `yoton.py` + `payloads/ymon.c` (own ADFU monitor v2): any-address read/write, payloads run in thread mode with fault
  capture and host-side abort (`state`, `run`, `read`, `write`, `nordump [--descramble]`, `boot`, `screenshot`).
* Ghidra headless is blocked by the session's permission classifier; `tools/xref.py` + objdump used instead.

## 12. Rockbox
* Measured: Rockbox core for ARMv7-M = **858 KB code + 731 KB BSS** (Echo R1 build). Cannot run in 320 KB RAM without XIP
  (none: the vendor OS pages 1–8 KB code banks from NOR in software).
* New target `yotonym03` in `ext/rockbox` (configure id 305 / model 134): `firmware/target/arm/atj2157/…`,
  `firmware/export/{atj2157.h,config/yotonym03.h}`, `bootloader/yotonym03.c`. The bootloader builds (≈32 KB) and is laid out
  at 0x124000 so ymon can load it (`yoton.py boot build/ym03-boot/bootloader.bin --test`).

## 13. Port status (2026-11-23)

Rockbox runs standalone on the YM03 from NOR; user-validated on hardware.

**Boot.** NOR sector 0 holds a 512-byte stage 1 (`payloads/ym03boot1.c`, own PIO SPI read) followed by the
untouched vendor bytes 0x200-0xFFF. Stage 2 (`payloads/ym03stub.c`, linked at 0x102400, NOR 0x7DF400) picks:
RAM marker "ADFU" or Right held → boot-ROM recovery (ADFU); Select held → stock firmware; three failed Rockbox
starts → ADFU; otherwise Rockbox (resident image "YRES" at NOR 0x7CC000). `tools/ym03_install.py` stages and flashes;
`restore-vendor` puts the original sector back. `payloads/wdreset.c` reboots through the watchdog without a key.

**Demand paging** (`firmware/target/arm/atj2157/pager-atj2157.c`, `tools/ym03_pagelink.py`). Resident image ~76 KB
in SRAM0; the rest of the code (31 × 8 KB pages) and rodata (17 pages) live in a page store at NOR 0x762000.
Hardware aliases SRAM2 (code, 4 slots) and SRAM3 (rodata, 2 slots) every 32 KB, so each page is linked at its own
alias. The pager is a cache of 1 KB lines: MPU region 0 denies 0x200000-0x3FFFFF (SRAM3 0x300000-0x33FFFF stays
RAM), regions 1-7 are an LRU pool of mapped pages with sub-region masks; a fault DMA-reads one 1 KB line at
48 MHz SPI (178 µs) or just remaps a line still holding the page's data. Profile-guided packing
(`tools/ym03_hot.txt`: browser list path, directory scan path, then Now Playing) keeps working sets apart.

**Audio.** Internal DAC via DMA channel 5. Loadable codecs (fit the 23 KB codec area): AIFF, WAV (incl. ADPCM
variants), AU, ADX, VOX, MOD (SMAF builds but did not play an ffmpeg-made file); MP3/MP2/MP1 use a built-in
minimp3 decoder on the FPU (`apps/codec_builtin_mp3.c`, state in the codec area + plugin buffer). 320 kbps MP3
plays with Now Playing up (~15 % idle). Small-memory tuning: 8 × 2 KB PCM chunks, ~9.6 KB file ring, 2 KB guard,
buffering polls every 50 ms, UI yield points (list draw, LCD update, directory read) so long UI work cannot starve
the decoder.

**UI/IO.** 128×128 LCD, 9 keys on two resistor ladders (incl. Vol+/Vol-; the power key is not visible to software,
hold Back to power off), backlight PWM, SD card (CMD18/25, 24 MHz), USB mass storage (FOTG210 core, ~2.6 MB/s
read), timer, watchdog. Folders larger than the browser buffer (e.g. 3000 entries) are shown as sorted windows of
~50 entries with "<< Previous entries" / "More entries >>" (one directory pass, ~1-2 s, per move); a playlist
started in such a folder continues into the next window at its end (verified: tracks 58→120 across windows).

**Plugins.** Load into codec area + plugin buffer (27.5 KB; playback is stopped first), HM60x key layout
(Back = Power). 83 of 146 build and fit; installed to `.rockbox/rocks`. Chessbox fails (engine too deep for the
6 KB stack); the 19 largest plugins do not fit.

**Limits.** 320 KB RAM: no FLAC/Vorbis/AAC/WMA/Opus (FLAC: 24-32 KB decode buffers plus 8-12 KB frames vs a
~27 KB codec area and ~9 KB file ring; the others are larger still; Speex/WavPack are 13-14 KB over), no database, no loadable
language, playlists ≤ 500 entries, WAV/AIFF can drop out while browsing (CD-quality rate vs 9.6 KB file ring).

**Testing.** `tools/ym03_maintest.py` boots a build from RAM in test mode and reads results after the self-reboot:
`--wps`, `--ui` (injected keys), `--browse[-play]`, `--plugin N`, `--keys`, `--pcm`, `--faults`/`--rofaults`
(fault logs), `--dirtime`. Patches against upstream Rockbox: `patches/`.
