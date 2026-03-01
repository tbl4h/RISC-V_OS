

.PHONY: kernel opensbi run clean prepare-opensbi

# Default cross-compiler prefix (can be overridden on the make command line)
CROSS_COMPILE ?= riscv64-linux-gnu-
OPENSBI_DIR ?= opensbi
OPENSBI_REPO ?= https://github.com/riscv-software-src/opensbi.git
OPENSBI_REF ?= v1.4
SBI_INCLUDE ?= $(OPENSBI_DIR)/include
MY_SBI_INCLUDE ?= libs/sbi
MY_DTB_INCLUDE ?= libs/dtb
LIBS_INCLUDE ?= libs
DRIVERS_INCLUDE ?= drivers
LIBFDT ?= $(OPENSBI_DIR)/lib/utils/libfdt
DTB_INCLUDE ?= libs
KERNEL ?= kernel
KERNEL_ELF ?= kernel.elf
KERNEL_BIN ?= kernel.bin
KERNEL_CFLAGS ?= -ffreestanding -fno-pie -no-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mcmodel=medany
KERNEL_LDFLAGS ?= -nostdlib -static -no-pie -Wl,--build-id=none

KERNEL_SRCS = \
	kernel/entry.S \
	kernel/kernel.c \
	kernel/memory_map.c \
	kernel/platform_init.c \
	kernel/panic.c \
	drivers/uart/ns16550a.c \
	drivers/uart/uart_console.c \
	libs/dtb/dtb.c

LIBFDT_SRCS = \
	$(LIBFDT)/fdt.c \
	$(LIBFDT)/fdt_ro.c \
	$(LIBFDT)/fdt_rw.c \
	$(LIBFDT)/fdt_wip.c \
	$(LIBFDT)/fdt_addresses.c

OPENSBI_UTILS_SRCS = \
	$(OPENSBI_DIR)/lib/sbi/sbi_string.c

KERNEL_INCLUDES = \
	-I$(LIBS_INCLUDE) \
	-I$(SBI_INCLUDE) \
	-I$(MY_SBI_INCLUDE) \
	-I$(LIBFDT) \
	-I$(DTB_INCLUDE) \
	-I$(MY_DTB_INCLUDE) \
	-I$(DRIVERS_INCLUDE) \
	-I$(KERNEL)

OPENSBI_ECALL_HEADER ?= $(SBI_INCLUDE)/sbi/sbi_ecall_interface.h
LIBFDT_HEADER ?= $(LIBFDT)/libfdt.h

prepare-opensbi:
	@set -eu; \
	if [ -f "$(OPENSBI_ECALL_HEADER)" ] && [ -f "$(LIBFDT_HEADER)" ]; then \
		exit 0; \
	fi; \
	echo "Preparing OpenSBI in $(OPENSBI_DIR) (ref: $(OPENSBI_REF))"; \
	if [ -n "$$(ls -A "$(OPENSBI_DIR)" 2>/dev/null)" ] && [ ! -d "$(OPENSBI_DIR)/.git" ]; then \
		echo "Error: $(OPENSBI_DIR) exists and is not a git repository."; \
		echo "Remove its contents or set OPENSBI_DIR to a different path."; \
		exit 1; \
	fi; \
	mkdir -p "$(OPENSBI_DIR)"; \
	if [ ! -d "$(OPENSBI_DIR)/.git" ]; then \
		git -C "$(OPENSBI_DIR)" init; \
	fi; \
	if ! git -C "$(OPENSBI_DIR)" remote get-url origin >/dev/null 2>&1; then \
		git -C "$(OPENSBI_DIR)" remote add origin "$(OPENSBI_REPO)"; \
	fi; \
	git -C "$(OPENSBI_DIR)" fetch --depth 1 origin "$(OPENSBI_REF)"; \
	git -C "$(OPENSBI_DIR)" checkout --detach FETCH_HEAD

kernel: prepare-opensbi
	$(CROSS_COMPILE)gcc $(KERNEL_CFLAGS) $(KERNEL_LDFLAGS) $(KERNEL_INCLUDES) -T kernel/linker.ld $(KERNEL_SRCS) $(LIBFDT_SRCS) $(OPENSBI_UTILS_SRCS) -o $(KERNEL_ELF)
	$(CROSS_COMPILE)objcopy -O binary $(KERNEL_ELF) $(KERNEL_BIN)


opensbi: kernel
	$(MAKE) -C $(OPENSBI_DIR) PLATFORM=generic FW_PAYLOAD_PATH=../$(KERNEL_BIN) CROSS_COMPILE=$(CROSS_COMPILE)

run: opensbi
	qemu-system-riscv64 \
		-machine virt \
		-nographic \
		-bios $(OPENSBI_DIR)/build/platform/generic/firmware/fw_payload.bin

clean:
	rm -f $(KERNEL_ELF) $(KERNEL_BIN)
	@if [ -f "$(OPENSBI_DIR)/Makefile" ]; then \
		$(MAKE) -C "$(OPENSBI_DIR)" clean; \
	fi
