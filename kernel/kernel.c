#include <stdint.h>
#include <sbi/sbi.h>
#include <sbi/sbi_healper.h>
#include <sbi/sbi_hart state management extension.h>
#include <sbi/sbi_string.h>
#include <panic.h>
#include <uart/uart_console.h>
#include <platform_init.h>

extern char _bss_start[];
extern char _bss_end[];
extern char _kernel_start[];
extern char _kernel_end[];
extern char _text_start[];
extern char _text_end[];
extern char _rodata_start[];
extern char _rodata_end[];
extern char _data_start[];
extern char _data_end[];

static hw_state_t g_hw;


static inline void clear_bss(void)
{
    uintptr_t start = (uintptr_t)_bss_start;
    uintptr_t end   = (uintptr_t)_bss_end;

    sbi_memset((void*)start, 0, end - start);
}

static inline void idle_forever(void)
{
    while (1)
        asm volatile("wfi");
}

void kmain(uint64_t hartid, void *dtb)
{
    clear_bss();
    if (hartid != 0)
        idle_forever();

    g_hw.boot_hartid = (uint32_t)hartid;

    init_dtb(&g_hw, dtb, hartid);
    {
        int uart_err = uart_console_init_from_dtb();
        if (uart_err)
            panic(uart_console_strerror(uart_err));
    }
    uart_console_puts("[kernel] uart initialized\n");
    validate_and_dump_dtb_state(&g_hw);
    init_sbi(&g_hw);
    uart_console_puts("[kernel] sbi ready\n");

    {
        
        struct sbiret hs = sbi_hart_get_status(hartid);
        if (hs.error < 0 || hs.value != SBI_HSM_STATE_STARTED)
            panic("Boot hart is not STARTED");
    }

    init_timer();
    uart_console_puts("[kernel] timer ready\n");
    idle_forever();
}
