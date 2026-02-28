#include <stdint.h>
#include <sbi/sbi_base.h>
#include <sbi/sbi_system reset extension.h>
#include <sbi/sbi_ipi.h>
#include <panic.h>
#include <uart/uart_console.h>


static void stop_other_harts(void)
{
    struct sbiret ret;

    ret = sbi_probe_extension(SBI_EXT_IPI);
    if (ret.error == 0 && ret.value > 0) {
        /* hart_mask = wszystkie */
        sbi_send_ipi(~0UL, 0);
    }
}

__attribute__((noreturn))
void panic(const char *msg)
{
    disable_interrupts();

    if (uart_console_is_ready()) {
        uart_console_puts("\n[panic] ");
        if (msg)
            uart_console_puts(msg);
        else
            uart_console_puts("(null)");
        uart_console_puts("\n");
    }

    /* Zachowaj wskaźnik do komunikatu w rejestrze,
       aby debugger mógł go odczytać */
    register const char *panic_msg asm("t0") = msg;
    (void)panic_msg;

    stop_other_harts();

    /* Jeśli mamy SRST — spróbuj wyłączyć system */
    struct sbiret ret = sbi_probe_extension(SBI_EXT_SRST);
    if (ret.error == 0 && ret.value > 0) {
        sbi_system_reset(SBI_SRST_RESET_TYPE_SHUTDOWN,
                         SBI_SRST_RESET_REASON_NONE);
    }

#ifdef DEBUG
    /* Jeśli jesteś pod QEMU + GDB */
    debug_break();
#endif

    /* Ostateczny bezpieczny stan */
    while (1)
        asm volatile("wfi");
}

/*TODO
Co dostaniesz po implementacji UART?

Gdy zrobisz driver UART, rozszerzymy panic o:

wypisywanie rejestrów

dump scause/sepc/stval

dump satp

stack trace

zatrzymanie wszystkich hartów przez HSM
*/
