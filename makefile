

.PHONY: kernel opensbi run clean

# Default cross-compiler prefix (can be overridden on the make command line)
CROSS_COMPILE ?= riscv64-linux-gnu-
SBI_INCLUDE ?= opensbi/include
LIBS_INCLUDE ?= libs
LIBFDT ?= opensbi/lib/utils/libfdt
DTB_INCLUDE ?= libs
KERNEL ?= kernel

KERNEL_SRCS = \
	kernel/entry.S \
	kernel/kernel.c \
	kernel/panic.c \
	libs/dtb/dtb.c

LIBFDT_SRCS = \
	opensbi/lib/utils/libfdt/fdt.c \
	opensbi/lib/utils/libfdt/fdt_ro.c \
	opensbi/lib/utils/libfdt/fdt_rw.c \
	opensbi/lib/utils/libfdt/fdt_wip.c \
	opensbi/lib/utils/libfdt/fdt_addresses.c

OPENSBI_UTILS_SRCS = \
	opensbi/lib/sbi/sbi_string.c

KERNEL_INCLUDES = \
	-I$(LIBS_INCLUDE) \
	-I$(SBI_INCLUDE) \
	-I$(LIBFDT) \
	-I$(DTB_INCLUDE) \
	-I$(KERNEL)

kernel:
	$(CROSS_COMPILE)gcc -nostdlib $(KERNEL_INCLUDES) -T kernel/linker.ld $(KERNEL_SRCS) $(LIBFDT_SRCS) $(OPENSBI_UTILS_SRCS) -o kernel.elf


opensbi: kernel
	make -C opensbi PLATFORM=generic FW_PAYLOAD_PATH=../kernel.elf CROSS_COMPILE=$(CROSS_COMPILE)

run: opensbi
	qemu-system-riscv64 \
		-machine virt \
		-nographic \
		-bios opensbi/build/platform/generic/firmware/fw_payload.bin

clean:
	rm -f kernel.elf
	make -C opensbi clean
