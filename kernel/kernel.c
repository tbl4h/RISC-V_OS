#include <stdint.h>
#include <dtb/dtb.h>
#include <sbi/sbi.h>
#include <sbi/sbi_healper.h>
#include <sbi/sbi_base.h>
#include <sbi/sbi_hart state management extension.h>
#include <sbi/sbi_timer.h>
#include <panic.h>
#include <uart/uart_console.h>

typedef struct {
    uint32_t boot_hartid;
    int boot_cpu_node;
    int cpu_count;
    uint64_t mem_base;
    uint64_t mem_size;
    uint64_t mem_total;
    uint32_t timebase_hz;
    int timer_node;
    int plic_node;
    int clint_node;
    int imsic_node;
} hw_state_t;

static hw_state_t g_hw;

static inline void idle_forever(void)
{
    while (1)
        asm volatile("wfi");
}

static void init_sbi(int cpu_count)
{
    struct sbiret spec = sbi_get_spec_version();
    if (spec.error < 0)
        panic("SBI BASE extension is not available");

    require_extension(SBI_EXT_TIME, "No TIME");
    require_extension(SBI_EXT_HSM, "No HSM");

    if (cpu_count > 1) {
        require_extension(SBI_EXT_IPI, "No IPI");
        require_extension(SBI_EXT_RFENCE, "No RFENCE");
    }
}

static void init_dtb(void *dtb, uint64_t hartid)
{
    int err;

    if (!dtb)
        panic("No DTB");

    err = dtb_init(dtb);
    if (err)
        panic("dtb_init failed");

    err = dtb_get_cpu_count(&g_hw.cpu_count);
    if (err || g_hw.cpu_count <= 0)
        panic("There is no CPU in DTB");

    err = dtb_cpu_find_hart((uint32_t)hartid, &g_hw.boot_cpu_node);
    if (err)
        panic("Boot hart nie istnieje w DTB");

    err = dtb_get_memory(&g_hw.mem_base, &g_hw.mem_size);
    if (err)
        panic("There is no DTB memory node");

    err = dtb_memory_total(&g_hw.mem_total);
    if (err)
        panic("Error getting total size of RAM");

    err = dtb_get_timebase(&g_hw.timebase_hz);
    if (err || !g_hw.timebase_hz)
        panic("There is no timebase-frequency");

    err = dtb_get_timer_node(&g_hw.timer_node);
    if (err)
        panic("There is no timer node in DTB");

    g_hw.plic_node = -1;
    g_hw.clint_node = -1;
    g_hw.imsic_node = -1;
    dtb_detect_plic(&g_hw.plic_node);
    dtb_detect_clint(&g_hw.clint_node);
    dtb_detect_imsic(&g_hw.imsic_node);
}

static void init_timer(void)
{
    struct sbiret ret = sbi_set_timer(~0ULL);
    if (ret.error < 0)
        panic("set_timer failed.");
}

static void validate_and_dump_dtb_state(void)
{
    int ok = 1;

    if (!uart_console_is_ready())
        return;

    uart_console_puts("\n[dtb] validation\n");
    uart_console_puts("  dtb_get: ");
    uart_console_put_hex_u64((uint64_t)(uintptr_t)dtb_get());
    uart_console_puts("\n");

    uart_console_puts("  cpu_count: ");
    uart_console_put_dec_i32(g_hw.cpu_count);
    if (g_hw.cpu_count <= 0) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  boot_cpu_node: ");
    uart_console_put_dec_i32(g_hw.boot_cpu_node);
    if (g_hw.boot_cpu_node < 0) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  mem_base: ");
    uart_console_put_hex_u64(g_hw.mem_base);
    uart_console_puts("  mem_size: ");
    uart_console_put_hex_u64(g_hw.mem_size);
    uart_console_puts("  mem_total: ");
    uart_console_put_hex_u64(g_hw.mem_total);
    if (!g_hw.mem_base || !g_hw.mem_size || g_hw.mem_total < g_hw.mem_size) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  timebase_hz: ");
    uart_console_put_dec_u32(g_hw.timebase_hz);
    if (!g_hw.timebase_hz) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  timer_node: ");
    uart_console_put_dec_i32(g_hw.timer_node);
    if (g_hw.timer_node < 0) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_dump_info();

    uart_console_puts("  plic_node: ");
    uart_console_put_dec_i32(g_hw.plic_node);
    uart_console_puts("  clint_node: ");
    uart_console_put_dec_i32(g_hw.clint_node);
    uart_console_puts("  imsic_node: ");
    uart_console_put_dec_i32(g_hw.imsic_node);
    uart_console_puts("\n");

    uart_console_puts("  overall: ");
    uart_console_puts(ok ? "OK\n" : "FAIL\n");
}

void kmain(uint64_t hartid, void *dtb)
{
    if (hartid != 0)
        idle_forever();

    g_hw.boot_hartid = (uint32_t)hartid;

    init_dtb(dtb, hartid);
    {
        int uart_err = uart_console_init_from_dtb();
        if (uart_err)
            panic(uart_console_strerror(uart_err));
    }
    uart_console_puts("[kernel] uart initialized\n");
    validate_and_dump_dtb_state();
    init_sbi(g_hw.cpu_count);
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
