#include <stdint.h>
#include <dtb/dtb.h>
#include <sbi/sbi.h>
#include <sbi/sbi_healper.h>
#include <sbi/sbi_base.h>
#include <sbi/sbi_timer.h>
#include <panic.h>
#include <uart/uart_console.h>
#include <platform_init.h>

void init_sbi(const hw_state_t *hw)
{
    struct sbiret spec = sbi_get_spec_version();
    if (spec.error < 0)
        panic("SBI BASE extension is not available");

    require_extension(SBI_EXT_TIME, "No TIME");
    require_extension(SBI_EXT_HSM, "No HSM");

    if (hw->cpu_count > 1) {
        require_extension(SBI_EXT_IPI, "No IPI");
        require_extension(SBI_EXT_RFENCE, "No RFENCE");
    }
}

void init_dtb(hw_state_t *hw, void *dtb, uint64_t hartid)
{
    int err;

    if (!dtb)
        panic("No DTB");

    err = dtb_init(dtb);
    if (err)
        panic("dtb_init failed");

    err = dtb_get_cpu_count(&hw->cpu_count);
    if (err || hw->cpu_count <= 0)
        panic("There is no CPU in DTB");

    err = dtb_cpu_find_hart((uint32_t)hartid, &hw->boot_cpu_node);
    if (err)
        panic("Boot hart nie istnieje w DTB");

    err = dtb_get_memory(&hw->mem_base, &hw->mem_size);
    if (err)
        panic("There is no DTB memory node");

    err = dtb_memory_total(&hw->mem_total);
    if (err)
        panic("Error getting total size of RAM");

    err = dtb_get_timebase(&hw->timebase_hz);
    if (err || !hw->timebase_hz)
        panic("There is no timebase-frequency");

    err = dtb_get_timer_node(&hw->timer_node);
    if (err)
        panic("There is no timer node in DTB");

    hw->plic_node = -1;
    hw->clint_node = -1;
    hw->imsic_node = -1;
    dtb_detect_plic(&hw->plic_node);
    dtb_detect_clint(&hw->clint_node);
    dtb_detect_imsic(&hw->imsic_node);
}

void init_timer(void)
{
    struct sbiret ret = sbi_set_timer(~0ULL);
    if (ret.error < 0)
        panic("set_timer failed.");
}

void validate_and_dump_dtb_state(const hw_state_t *hw)
{
    int ok = 1;

    if (!uart_console_is_ready())
        return;

    uart_console_puts("\n[dtb] validation\n");
    uart_console_puts("  dtb_get: ");
    uart_console_put_hex_u64((uint64_t)(uintptr_t)dtb_get());
    uart_console_puts("\n");

    uart_console_puts("  cpu_count: ");
    uart_console_put_dec_i32(hw->cpu_count);
    if (hw->cpu_count <= 0) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  boot_cpu_node: ");
    uart_console_put_dec_i32(hw->boot_cpu_node);
    if (hw->boot_cpu_node < 0) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  mem_base: ");
    uart_console_put_hex_u64(hw->mem_base);
    uart_console_puts("  mem_size: ");
    uart_console_put_hex_u64(hw->mem_size);
    uart_console_puts("  mem_total: ");
    uart_console_put_hex_u64(hw->mem_total);
    if (!hw->mem_base || !hw->mem_size || hw->mem_total < hw->mem_size) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  timebase_hz: ");
    uart_console_put_dec_u32(hw->timebase_hz);
    if (!hw->timebase_hz) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_puts("  timer_node: ");
    uart_console_put_dec_i32(hw->timer_node);
    if (hw->timer_node < 0) {
        uart_console_puts("  FAIL\n");
        ok = 0;
    } else {
        uart_console_puts("  OK\n");
    }

    uart_console_dump_info();

    uart_console_puts("  plic_node: ");
    uart_console_put_dec_i32(hw->plic_node);
    uart_console_puts("  clint_node: ");
    uart_console_put_dec_i32(hw->clint_node);
    uart_console_puts("  imsic_node: ");
    uart_console_put_dec_i32(hw->imsic_node);
    uart_console_puts("\n");

    uart_console_puts("  overall: ");
    uart_console_puts(ok ? "OK\n" : "FAIL\n");
}
