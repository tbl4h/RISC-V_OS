/*
UART driver for NS16550A UART controller.
*/
#ifndef DRIVERS_UART_NS16550A_H
#define DRIVERS_UART_NS16550A_H

#include <stdint.h>

typedef struct {
    uint64_t base;
    uint64_t input_clock_hz;
    uint32_t baud_rate;
    uint32_t reg_shift;
    uint32_t reg_io_width;
} ns16550a_config_t;

enum {
    NS16550A_ERR_BADVALUE = -1000,
    NS16550A_ERR_UNSUPPORTED_WIDTH,
    NS16550A_ERR_TIMEOUT,
    NS16550A_TRY_GETC_NO_DATA = 1,
};

int ns16550a_init(const ns16550a_config_t *cfg);
int ns16550a_is_ready(void);
const char *ns16550a_strerror(int err);

void ns16550a_putc(char c);
void ns16550a_puts(const char *s);
void ns16550a_write(const char *buf, uint64_t len);
int ns16550a_try_getc(char *out);
void ns16550a_put_hex_u64(uint64_t v);
void ns16550a_put_dec_u32(uint32_t v);
void ns16550a_put_dec_i32(int v);

#endif

/*TODO
Rozważyć zamiast active pooling użycie przerwań, ale to może być trudne do 
implementacji w sposób przenośny i może wymagać dodatkowej konfiguracji sprzętowej.
Active pooling jest prostszy do implementacji i działa na większości platform, 
ale może być mniej wydajny, ponieważ procesor musi stale sprawdzać stan rejestru LSR.

To jest wersja “early console”: brak przerwań, brak obsługi błędów RX/TX z LSR, brak
konfiguracji parity/stop bits poza 8N1, brak locków (dla wielu hartów trzeba
będzie dodać synchronizację).
*/
