#include <stdint.h>
#include <dtb/dtb.h>
#include <libfdt.h>
#include <uart/ns16550a.h>
#include <uart/uart_console.h>

static uart_console_info_t g_uart_console_info;
static int g_uart_console_ready;
static const uart_console_backend_t *g_uart_console_backend;

static int ns16550a_backend_init_from_info(const uart_console_info_t *info)
{
    ns16550a_config_t cfg;

    if (!info)
        return NS16550A_ERR_BADVALUE;

    cfg.base = info->base;
    cfg.input_clock_hz = info->input_clock_hz;
    cfg.baud_rate = info->baud_rate;
    cfg.reg_shift = info->reg_shift;
    cfg.reg_io_width = info->reg_io_width;

    return ns16550a_init(&cfg);
}

static const uart_console_backend_t g_ns16550a_backend = {
    .init_from_info = ns16550a_backend_init_from_info,
    .is_ready = ns16550a_is_ready,
    .putc = ns16550a_putc,
    .puts = ns16550a_puts,
    .write = ns16550a_write,
    .try_getc = ns16550a_try_getc,
    .put_hex_u64 = ns16550a_put_hex_u64,
    .put_dec_u32 = ns16550a_put_dec_u32,
    .put_dec_i32 = ns16550a_put_dec_i32,
};

static void uart_console_set_defaults(uart_console_info_t *info)
{
    info->node = -1;
    info->base = 0;
    info->size = 0;
    info->input_clock_hz = 0;
    info->baud_rate = 0;
    info->reg_shift = 0;
    info->reg_io_width = 1;
}

static int uart_console_parse_baud_from_stdout_path(uint32_t *baud)
{
    const void *fdt;
    const char *stdout_path;
    uint64_t value = 0;
    int chosen;
    int len;
    int i;
    int has_digit = 0;

    if (!baud)
        return UART_CONSOLE_ERR_BADVALUE;

    fdt = dtb_get();
    if (!fdt)
        return UART_CONSOLE_ERR_DTB_NOT_READY;

    chosen = fdt_path_offset(fdt, "/chosen");
    if (chosen < 0)
        return chosen;

    stdout_path = fdt_getprop(fdt, chosen, "stdout-path", &len);
    if (!stdout_path)
        return len;

    for (i = 0; i < len && stdout_path[i] && stdout_path[i] != ':'; i++)
        ;
    if (i >= len || stdout_path[i] != ':')
        return -FDT_ERR_NOTFOUND;
    i++;

    for (; i < len; i++) {
        char c = stdout_path[i];
        if (c < '0' || c > '9')
            break;
        has_digit = 1;
        value = (value * 10u) + (uint64_t)(c - '0');
        if (value > 0xFFFFFFFFULL)
            return -FDT_ERR_BADVALUE;
    }

    if (!has_digit)
        return -FDT_ERR_NOTFOUND;

    *baud = (uint32_t)value;
    return 0;
}

int uart_console_probe_from_dtb(uart_console_info_t *info)
{
    int err;
    int node = -1;
    uint64_t base = 0;
    uint64_t size = 0;

    if (!info)
        return UART_CONSOLE_ERR_BADVALUE;
    if (!dtb_get())
        return UART_CONSOLE_ERR_DTB_NOT_READY;

    uart_console_set_defaults(info);

    err = dtb_chosen_stdout(&node);
    if (!err) {
        err = dtb_decode_reg(node, 0, &base, &size);
        if (err)
            node = -1;
    }

    if (node < 0) {
        err = dtb_uart_ns16550a(&node, &base, &size);
        if (err)
            return UART_CONSOLE_ERR_DTB_UART_NOT_FOUND;
    }

    if (!base)
        return UART_CONSOLE_ERR_DTB_UART_REG_INVALID;

    info->node = node;
    info->base = base;
    info->size = size;

    dtb_get_clock_frequency(node, &info->input_clock_hz);
    if (dtb_get_u32(node, "current-speed", &info->baud_rate))
        uart_console_parse_baud_from_stdout_path(&info->baud_rate);
    if (!info->baud_rate)
        info->baud_rate = 115200;
    dtb_get_u32(node, "reg-shift", &info->reg_shift);
    if (dtb_get_u32(node, "reg-io-width", &info->reg_io_width))
        info->reg_io_width = 1;

    return 0;
}

int uart_console_set_backend(const uart_console_backend_t *backend)
{
    if (!backend)
        return UART_CONSOLE_ERR_BADVALUE;
    if (!backend->init_from_info || !backend->is_ready || !backend->putc ||
        !backend->puts || !backend->write || !backend->try_getc ||
        !backend->put_hex_u64 || !backend->put_dec_u32 || !backend->put_dec_i32) {
        return UART_CONSOLE_ERR_BADVALUE;
    }

    g_uart_console_backend = backend;
    g_uart_console_ready = 0;
    return 0;
}

int uart_console_init_from_dtb(void)
{
    int err;

    g_uart_console_ready = 0;
    uart_console_set_defaults(&g_uart_console_info);
    if (!g_uart_console_backend)
        g_uart_console_backend = &g_ns16550a_backend;

    err = uart_console_probe_from_dtb(&g_uart_console_info);
    if (err)
        return err;

    err = g_uart_console_backend->init_from_info(&g_uart_console_info);
    if (err)
        return UART_CONSOLE_ERR_UART_INIT_FAILED;

    g_uart_console_ready = 1;
    return 0;
}

int uart_console_is_ready(void)
{
    return g_uart_console_ready && g_uart_console_backend && g_uart_console_backend->is_ready();
}

const uart_console_info_t *uart_console_info(void)
{
    return &g_uart_console_info;
}

void uart_console_dump_info(void)
{
    const uart_console_info_t *info;

    if (!uart_console_is_ready())
        return;

    info = uart_console_info();
    uart_console_puts("  uart_node: ");
    uart_console_put_dec_i32(info->node);
    uart_console_puts("  uart_base: ");
    uart_console_put_hex_u64(info->base);
    uart_console_puts("  uart_size: ");
    uart_console_put_hex_u64(info->size);
    uart_console_puts("\n");

    uart_console_puts("  uart_clock_hz: ");
    uart_console_put_hex_u64(info->input_clock_hz);
    uart_console_puts("  uart_baud: ");
    uart_console_put_dec_u32(info->baud_rate);
    uart_console_puts("  reg_shift: ");
    uart_console_put_dec_u32(info->reg_shift);
    uart_console_puts("  io_width: ");
    uart_console_put_dec_u32(info->reg_io_width);
    uart_console_puts("\n");
}

int uart_console_putc(char c)
{
    if (!uart_console_is_ready())
        return UART_CONSOLE_ERR_NOT_READY;
    g_uart_console_backend->putc(c);
    return 0;
}

int uart_console_puts(const char *s)
{
    if (!s)
        return UART_CONSOLE_ERR_BADVALUE;
    if (!uart_console_is_ready())
        return UART_CONSOLE_ERR_NOT_READY;
    g_uart_console_backend->puts(s);
    return 0;
}

int uart_console_write(const char *buf, uint64_t len)
{
    if (!buf)
        return UART_CONSOLE_ERR_BADVALUE;
    if (!uart_console_is_ready())
        return UART_CONSOLE_ERR_NOT_READY;
    g_uart_console_backend->write(buf, len);
    return 0;
}

int uart_console_try_getc(char *out)
{
    if (!out)
        return UART_CONSOLE_ERR_BADVALUE;
    if (!uart_console_is_ready())
        return UART_CONSOLE_ERR_NOT_READY;
    return g_uart_console_backend->try_getc(out);
}

void uart_console_put_hex_u64(uint64_t value)
{
    if (!uart_console_is_ready())
        return;
    g_uart_console_backend->put_hex_u64(value);
}

void uart_console_put_dec_u32(uint32_t value)
{
    if (!uart_console_is_ready())
        return;
    g_uart_console_backend->put_dec_u32(value);
}

void uart_console_put_dec_i32(int value)
{
    if (!uart_console_is_ready())
        return;
    g_uart_console_backend->put_dec_i32(value);
}

const char *uart_console_strerror(int err)
{
    switch (err) {
    case 0:
        return "OK";
    case UART_CONSOLE_ERR_BADVALUE:
        return "Bad argument";
    case UART_CONSOLE_ERR_DTB_NOT_READY:
        return "DTB not initialized";
    case UART_CONSOLE_ERR_DTB_UART_NOT_FOUND:
        return "UART node not found in DTB";
    case UART_CONSOLE_ERR_DTB_UART_REG_INVALID:
        return "UART reg/base invalid";
    case UART_CONSOLE_ERR_UART_INIT_FAILED:
        return "NS16550A init failed";
    case UART_CONSOLE_ERR_NOT_READY:
        return "UART console is not ready";
    default:
        return "Unknown UART console error";
    }
}
