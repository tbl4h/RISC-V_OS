

.PHONY: kernel opensbi run clean

# Default cross-compiler prefix (can be overridden on the make command line)
CROSS_COMPILE ?= riscv64-linux-gnu-
SBI_INCLUDE ?= opensbi/include
LIBS_INCLUDE ?= libs

kernel:
	$(CROSS_COMPILE)gcc -nostdlib -I$(LIBS_INCLUDE) -I$(SBI_INCLUDE) -T kernel/linker.ld kernel/entry.S kernel/kernel.c -o kernel.elf


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
