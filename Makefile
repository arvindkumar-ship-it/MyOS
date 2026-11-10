# MyOS Makefile (simplified from limine-c-template: x86_64 + BIOS boot only).
#
#   make          build myos.iso
#   make run      boot the ISO in QEMU, serial output on this terminal
#   make debug    same, but QEMU waits for gdb on :1234 and logs interrupts to qemu.log
#   make clean    remove build output
#   make distclean  also remove downloaded deps

# Delete a target if its recipe fails, so we never keep half-built files.
.DELETE_ON_ERROR:
# Turn off make's built-in rules; we write every rule ourselves.
.SUFFIXES:

# ---------------------------------------------------------------------------
# Pinned dependency versions (pinned so the build does not break by itself later)
# ---------------------------------------------------------------------------
LIMINE_VERSION          := v12.9.6
LIMINE_PROTOCOL_COMMIT  := 6e85ddd6f928b5b512015c4d47d1cfce2fd28079

DEPS_DIR        := deps
LIMINE_DIR      := $(DEPS_DIR)/limine
LIMINE_TOOL     := $(LIMINE_DIR)/limine
LIMINE_H_DIR    := $(DEPS_DIR)/limine-protocol/include

# ---------------------------------------------------------------------------
# Output files
# ---------------------------------------------------------------------------
BUILD_DIR := build
KERNEL    := $(BUILD_DIR)/myos.elf
ISO       := myos.iso

# ---------------------------------------------------------------------------
# Toolchain and flags
# ---------------------------------------------------------------------------
CC := gcc

# Only gcc's own freestanding headers (stdint.h, stddef.h, stdbool.h, stdarg.h).
# -nostdinc hides the host libc headers so we cannot include <stdio.h> by mistake.
GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

# -ffreestanding        no libc, no hosted assumptions
# -fno-stack-protector  no stack canary (it needs runtime support we do not have)
# -fno-pic -fno-pie     plain absolute code; Ubuntu gcc builds PIE by default
# -mno-red-zone         interrupts would overwrite the 128-byte red zone below rsp
# -mcmodel=kernel       code and data live in the top 2 GiB (0xffffffff80000000+)
# -mno-sse -mno-mmx -mno-80387  never touch FPU/SIMD registers in the kernel
CFLAGS := -g -O2 -pipe -Wall -Wextra -std=gnu11 \
    -ffreestanding -fno-stack-protector -fno-stack-check \
    -fno-pic -fno-pie -fno-common \
    -m64 -march=x86-64 -mno-red-zone -mcmodel=kernel \
    -mno-sse -mno-mmx -mno-80387

# -MMD -MP: write .d files so changing a header rebuilds the .c files using it.
CPPFLAGS := -nostdinc -isystem $(GCC_INCLUDE) \
    -I kernel -I $(LIMINE_H_DIR) \
    -MMD -MP

LDFLAGS := -nostdlib -static -no-pie \
    -Wl,-z,max-page-size=0x1000 \
    -Wl,-z,noexecstack \
    -Wl,--build-id=sha1 \
    -Wl,-T,linker.ld

# Every .c file under kernel/ is compiled. build/ mirrors the source tree.
SRCS := $(shell find kernel -name '*.c')
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)

# ---------------------------------------------------------------------------
# QEMU (commands from docs/os-kernel-guide.md)
# ---------------------------------------------------------------------------
QEMU      := qemu-system-x86_64
QEMUFLAGS := -cdrom $(ISO) -m 512M -serial stdio -no-reboot -no-shutdown

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all run debug clean distclean

all: $(ISO)

run: $(ISO)
	$(QEMU) $(QEMUFLAGS)

debug: $(ISO)
	$(QEMU) $(QEMUFLAGS) -s -S -d int,cpu_reset -D qemu.log

# Compile one C file. Objects depend on limine.h being downloaded first.
$(BUILD_DIR)/%.o: %.c | $(LIMINE_H_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Link all objects into the kernel ELF, using our linker script.
$(KERNEL): $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

# Build a bootable ISO:
#  1. lay out the files in iso_root/
#  2. xorriso turns the folder into an ISO with an El Torito (CD) boot image
#  3. "limine bios-install" patches the ISO so BIOS can boot it from a hard disk/USB too
$(ISO): $(KERNEL) limine.conf $(LIMINE_TOOL)
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp $(KERNEL) iso_root/boot/kernel
	cp limine.conf iso_root/boot/limine/
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $@
	$(LIMINE_TOOL) bios-install $@
	rm -rf iso_root

# ---------------------------------------------------------------------------
# Dependencies (downloaded once into deps/, which is git-ignored)
# ---------------------------------------------------------------------------

# Limine prebuilt bootloader files + source of the small "limine" host tool.
$(LIMINE_DIR)/limine.c:
	rm -rf $(LIMINE_DIR) $(DEPS_DIR)/limine-binary.tar.gz
	mkdir -p $(LIMINE_DIR)
	curl -fL -o $(DEPS_DIR)/limine-binary.tar.gz \
		https://github.com/Limine-Bootloader/Limine/releases/download/$(LIMINE_VERSION)/limine-binary.tar.gz
	tar -xzf $(DEPS_DIR)/limine-binary.tar.gz -C $(LIMINE_DIR) --strip-components=1
	rm $(DEPS_DIR)/limine-binary.tar.gz
	touch $@

# Build the "limine" host tool (runs on Linux, not in our kernel).
$(LIMINE_TOOL): $(LIMINE_DIR)/limine.c
	$(MAKE) -C $(LIMINE_DIR)

# limine.h: the C structs for the Limine boot protocol.
$(LIMINE_H_DIR):
	rm -rf $(DEPS_DIR)/limine-protocol
	git clone -q https://github.com/Limine-Bootloader/limine-protocol.git $(DEPS_DIR)/limine-protocol
	git -C $(DEPS_DIR)/limine-protocol checkout -q $(LIMINE_PROTOCOL_COMMIT)

clean:
	rm -rf $(BUILD_DIR) iso_root $(ISO) qemu.log

distclean: clean
	rm -rf $(DEPS_DIR)

# Header dependency files written by -MMD.
-include $(OBJS:.o=.d)
