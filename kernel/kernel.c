#include <stdint.h>
#include <dtb/dtb.h>
#include <sbi/sbi.h>
#include <sbi/sbi_healper.h>
#include <sbi/sbi_base.h>
#include <sbi/sbi_hart state management extension.h>
#include <sbi/sbi_timer.h>
#include <panic.h>

typedef struct {
    uint32_t boot_hartid;
    int boot_cpu_node;
    int cpu_count;
    uint64_t mem_base;
    uint64_t mem_size;
    uint64_t mem_total;
    uint32_t timebase_hz;
    int timer_node;
    int uart_node;
    uint64_t uart_base;
    uint64_t uart_size;
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
        panic("SBI BASE niedostepne");

    require_extension(SBI_EXT_TIME, "Brak TIME");
    require_extension(SBI_EXT_HSM, "Brak HSM");

    if (cpu_count > 1) {
        require_extension(SBI_EXT_IPI, "Brak IPI");
        require_extension(SBI_EXT_RFENCE, "Brak RFENCE");
    }
}

static void init_dtb(void *dtb, uint64_t hartid)
{
    int err;

    if (!dtb)
        panic("Brak DTB");

    err = dtb_init(dtb);
    if (err)
        panic("dtb_init nie powiodlo sie");

    err = dtb_get_cpu_count(&g_hw.cpu_count);
    if (err || g_hw.cpu_count <= 0)
        panic("Brak aktywnych CPU w DTB");

    err = dtb_cpu_find_hart((uint32_t)hartid, &g_hw.boot_cpu_node);
    if (err)
        panic("Boot hart nie istnieje w DTB");

    err = dtb_get_memory(&g_hw.mem_base, &g_hw.mem_size);
    if (err)
        panic("Brak pamieci RAM w DTB");

    err = dtb_memory_total(&g_hw.mem_total);
    if (err)
        panic("Blad odczytu mapy RAM");

    err = dtb_get_timebase(&g_hw.timebase_hz);
    if (err || !g_hw.timebase_hz)
        panic("Brak timebase-frequency");

    err = dtb_get_timer_node(&g_hw.timer_node);
    if (err)
        panic("Brak wezla timera");

    g_hw.uart_node = -1;
    g_hw.uart_base = 0;
    g_hw.uart_size = 0;
    if (!dtb_chosen_stdout(&g_hw.uart_node)) {
        if (dtb_decode_reg(g_hw.uart_node, 0, &g_hw.uart_base, &g_hw.uart_size))
            g_hw.uart_node = -1;
    }
    if (g_hw.uart_node < 0)
        dtb_uart_ns16550a(&g_hw.uart_node, &g_hw.uart_base, &g_hw.uart_size);

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
        panic("set_timer nie powiodlo sie");
}

void kmain(uint64_t hartid, void *dtb)
{
    if (hartid != 0)
        idle_forever();

    g_hw.boot_hartid = (uint32_t)hartid;

    init_dtb(dtb, hartid);
    init_sbi(g_hw.cpu_count);

    {
        
        struct sbiret hs = sbi_hart_get_status(hartid);
        if (hs.error < 0 || hs.value != SBI_HSM_STATE_STARTED)
            panic("Boot hart nie jest STARTED");
    }

    init_timer();
    idle_forever();
}
