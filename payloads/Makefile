CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy
CFLAGS=-Os -Wall -ffreestanding -fno-builtin -march=armv7-m -mthumb -nostdlib -fno-pic
START?=0x11e000
%.bin: %.c fault.S rom.h
	$(CC) $(CFLAGS) -Wl,--defsym,IMAGE_START=$(START) -T link.ld fault.S $< -o $*.elf -lgcc
	$(OBJCOPY) -O binary $*.elf $@

ymon.bin: ymon.c ymon_s.S link.ld
	$(CC) $(CFLAGS) -Wl,--defsym,IMAGE_START=0x118000 -T link.ld ymon_s.S ymon.c -o ymon.elf -lgcc
	$(OBJCOPY) -O binary ymon.elf $@

# NOR boot stub: stage 1 (512-byte boot record) and stage 2 (1 KiB at NOR
# 0x7DF400); tools/ym03_install.py reads the .raw files
STUB_CFLAGS=-Os -Wall -ffreestanding -fno-builtin -march=armv7-m -mthumb -nostdlib -fno-pic

stub: ym03boot1.raw ym03stub2.raw

ym03boot1.raw: ym03boot1_start.S ym03boot1.c ym03boot1.ld
	$(CC) $(STUB_CFLAGS) -T ym03boot1.ld ym03boot1_start.S ym03boot1.c -o ym03boot1.elf
	$(OBJCOPY) -O binary -j .hdr -j .text ym03boot1.elf $@

ym03stub2.raw: ym03stub2_start.S ym03stub.c ym03stub2.ld
	$(CC) $(STUB_CFLAGS) -T ym03stub2.ld ym03stub2_start.S ym03stub.c -o ym03stub2.elf
	$(OBJCOPY) -O binary -j .hdr -j .text -j .tramp ym03stub2.elf $@
