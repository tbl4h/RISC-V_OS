#include <stdint.h>
#include <uart/ns16550a.h>

#define NS16550A_RBR_DLL_THR 0
#define NS16550A_IER_DLM 1
#define NS16550A_IIR_FCR 2
#define NS16550A_LCR 3
#define NS16550A_LSR 5

#define NS16550A_LCR_DLAB (1u << 7)
#define NS16550A_LSR_DR (1u << 0)
#define NS16550A_LSR_THRE (1u << 5)

typedef struct {
    uintptr_t base;
    uint32_t reg_shift;
    uint32_t reg_io_width;
    int ready;
} ns16550a_state_t;

static ns16550a_state_t g_uart;
static const uint32_t NS16550A_TX_TIMEOUT = 1000000u;

static inline uintptr_t ns16550a_reg_addr(uint32_t reg)
{
    return g_uart.base + ((uintptr_t)reg << g_uart.reg_shift);
}

static uint8_t ns16550a_reg_read(uint32_t reg)
{
    uintptr_t addr = ns16550a_reg_addr(reg);

    if (g_uart.reg_io_width == 4)
        return (uint8_t)(*(volatile uint32_t *)addr);
    if (g_uart.reg_io_width == 2)
        return (uint8_t)(*(volatile uint16_t *)addr);
    return *(volatile uint8_t *)addr;
}

static void ns16550a_reg_write(uint32_t reg, uint8_t value)
{
    uintptr_t addr = ns16550a_reg_addr(reg);

    if (g_uart.reg_io_width == 4)
        *(volatile uint32_t *)addr = (uint32_t)value;
    else if (g_uart.reg_io_width == 2)
        *(volatile uint16_t *)addr = (uint16_t)value;
    else
        *(volatile uint8_t *)addr = value;
}

static void ns16550a_putc_raw(char c)
{
    uint32_t spin = NS16550A_TX_TIMEOUT;

    if (!g_uart.ready)
        return;

    while (!(ns16550a_reg_read(NS16550A_LSR) & NS16550A_LSR_THRE)) {
        if (!spin)
            return;
        spin--;
    }

    ns16550a_reg_write(NS16550A_RBR_DLL_THR, (uint8_t)c);
}

static int ns16550a_valid_width(uint32_t width)
{
    return width == 1 || width == 2 || width == 4;
}

int ns16550a_init(const ns16550a_config_t *cfg)
{
    uint32_t baud;
    uint32_t divisor;
    uint32_t spin;
    uint8_t lcr;

    g_uart.ready = 0;

    if (!cfg || !cfg->base)
        return NS16550A_ERR_BADVALUE;
    if (!ns16550a_valid_width(cfg->reg_io_width))
        return NS16550A_ERR_UNSUPPORTED_WIDTH;

    g_uart.base = (uintptr_t)cfg->base;
    g_uart.reg_shift = cfg->reg_shift;
    g_uart.reg_io_width = cfg->reg_io_width;

    ns16550a_reg_write(NS16550A_IER_DLM, 0x00);

    baud = cfg->baud_rate ? cfg->baud_rate : 115200u;
    if (cfg->input_clock_hz != 0) {
        divisor = (uint32_t)(cfg->input_clock_hz / (16ULL * (uint64_t)baud));
        if (divisor == 0)
            divisor = 1;
        if (divisor > 0xFFFFu)
            divisor = 0xFFFFu;

        lcr = ns16550a_reg_read(NS16550A_LCR);
        ns16550a_reg_write(NS16550A_LCR, (uint8_t)(lcr | NS16550A_LCR_DLAB));
        ns16550a_reg_write(NS16550A_RBR_DLL_THR, (uint8_t)(divisor & 0xFFu));
        ns16550a_reg_write(NS16550A_IER_DLM, (uint8_t)((divisor >> 8) & 0xFFu));
    }

    ns16550a_reg_write(NS16550A_LCR, 0x03);
    ns16550a_reg_write(NS16550A_IIR_FCR, 0x07);

    spin = NS16550A_TX_TIMEOUT;
    while (!(ns16550a_reg_read(NS16550A_LSR) & NS16550A_LSR_THRE)) {
        if (!spin)
            return NS16550A_ERR_TIMEOUT;
        spin--;
    }

    g_uart.ready = 1;
    return 0;
}

int ns16550a_is_ready(void)
{
    return g_uart.ready;
}

const char *ns16550a_strerror(int err)
{
    switch (err) {
    case 0:
        return "OK";
    case NS16550A_TRY_GETC_NO_DATA:
        return "No RX data available";
    case NS16550A_ERR_BADVALUE:
        return "Bad config value";
    case NS16550A_ERR_UNSUPPORTED_WIDTH:
        return "Unsupported register width";
    case NS16550A_ERR_TIMEOUT:
        return "UART timeout";
    default:
        return "Unknown NS16550A error";
    }
}

void ns16550a_putc(char c)
{
    if (c == '\n')
        ns16550a_putc_raw('\r');
    ns16550a_putc_raw(c);
}

void ns16550a_puts(const char *s)
{
    if (!s)
        return;

    while (*s) {
        ns16550a_putc(*s);
        s++;
    }
}

void ns16550a_write(const char *buf, uint64_t len)
{
    uint64_t i;

    if (!buf)
        return;

    for (i = 0; i < len; i++)
        ns16550a_putc(buf[i]);
}

int ns16550a_try_getc(char *out)
{
    if (!out || !g_uart.ready)
        return NS16550A_ERR_BADVALUE;

    if (!(ns16550a_reg_read(NS16550A_LSR) & NS16550A_LSR_DR))
        return NS16550A_TRY_GETC_NO_DATA;

    *out = (char)ns16550a_reg_read(NS16550A_RBR_DLL_THR);
    return 0;
}

void ns16550a_put_hex_u64(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    ns16550a_puts("0x");
    for (i = 60; i >= 0; i -= 4)
        ns16550a_putc(hex[(value >> i) & 0xFULL]);
}

void ns16550a_put_dec_u32(uint32_t value)
{
    char buf[11];
    int i = 0;

    if (value == 0) {
        ns16550a_putc('0');
        return;
    }

    while (value && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i--)
        ns16550a_putc(buf[i]);
}

void ns16550a_put_dec_u64(uint64_t value)
{
    char buf[21];
    int i = 0;

    if (value == 0) {
        ns16550a_putc('0');
        return;
    }

    while (value && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i--)
        ns16550a_putc(buf[i]);
}

void ns16550a_put_dec_i32(int value)
{
    if (value < 0) {
        ns16550a_putc('-');
        ns16550a_put_dec_u32((uint32_t)(-value));
        return;
    }

    ns16550a_put_dec_u32((uint32_t)value);
}
