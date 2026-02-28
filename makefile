

.PHONY: kernel opensbi run clean

# Default cross-compiler prefix (can be overridden on the make command line)
CROSS_COMPILE ?= riscv64-linux-gnu-
SBI_INCLUDE ?= opensbi/include
MY_SBI_INCLUDE ?= libs/sbi
MY_DTB_INCLUDE ?= libs/dtb
LIBS_INCLUDE ?= libs
DRIVERS_INCLUDE ?= drivers
LIBFDT ?= opensbi/lib/utils/libfdt
DTB_INCLUDE ?= libs
KERNEL ?= kernel
KERNEL_ELF ?= kernel.elf
KERNEL_BIN ?= kernel.bin
KERNEL_CFLAGS ?= -ffreestanding -fno-pie -no-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mcmodel=medany
KERNEL_LDFLAGS ?= -nostdlib -static -no-pie -Wl,--build-id=none

KERNEL_SRCS = \
	kernel/entry.S \
	kernel/kernel.c \
	kernel/panic.c \
	drivers/uart/ns16550a.c \
	drivers/uart/uart_console.c \
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
	-I$(MY_SBI_INCLUDE) \
	-I$(LIBFDT) \
	-I$(DTB_INCLUDE) \
	-I$(MY_DTB_INCLUDE) \
	-I$(DRIVERS_INCLUDE) \
	-I$(KERNEL)

kernel:
	$(CROSS_COMPILE)gcc $(KERNEL_CFLAGS) $(KERNEL_LDFLAGS) $(KERNEL_INCLUDES) -T kernel/linker.ld $(KERNEL_SRCS) $(LIBFDT_SRCS) $(OPENSBI_UTILS_SRCS) -o $(KERNEL_ELF)
	$(CROSS_COMPILE)objcopy -O binary $(KERNEL_ELF) $(KERNEL_BIN)


opensbi: kernel
	make -C opensbi PLATFORM=generic FW_PAYLOAD_PATH=../$(KERNEL_BIN) CROSS_COMPILE=$(CROSS_COMPILE)

run: opensbi
	qemu-system-riscv64 \
		-machine virt \
		-nographic \
		-bios opensbi/build/platform/generic/firmware/fw_payload.bin

clean:
	rm -f $(KERNEL_ELF) $(KERNEL_BIN)
	make -C opensbi clean
