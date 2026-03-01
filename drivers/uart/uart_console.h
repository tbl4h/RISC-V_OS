/*
    UART console driver
*/
#ifndef DRIVERS_UART_UART_CONSOLE_H
#define DRIVERS_UART_UART_CONSOLE_H

#include <stdint.h>

typedef struct {
    int node;
    uint64_t base;
    uint64_t size;
    uint64_t input_clock_hz;
    uint32_t baud_rate;
    uint32_t reg_shift;
    uint32_t reg_io_width;
} uart_console_info_t;

typedef struct {
    int (*init_from_info)(const uart_console_info_t *info);
    int (*is_ready)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*write)(const char *buf, uint64_t len);
    int (*try_getc)(char *out);
    void (*put_hex_u64)(uint64_t value);
    void (*put_dec_u32)(uint32_t value);
    void (*put_dec_u64)(uint64_t value);
    void (*put_dec_i32)(int value);
} uart_console_backend_t;

enum {
    UART_CONSOLE_ERR_BADVALUE = -2000,
    UART_CONSOLE_ERR_DTB_NOT_READY,
    UART_CONSOLE_ERR_DTB_UART_NOT_FOUND,
    UART_CONSOLE_ERR_DTB_UART_REG_INVALID,
    UART_CONSOLE_ERR_UART_INIT_FAILED,
    UART_CONSOLE_ERR_NOT_READY,
};

int uart_console_set_backend(const uart_console_backend_t *backend);
int uart_console_probe_from_dtb(uart_console_info_t *info);
int uart_console_init_from_dtb(void);
int uart_console_is_ready(void);
const uart_console_info_t *uart_console_info(void);
void uart_console_dump_info(void);
int uart_console_putc(char c);
int uart_console_puts(const char *s);
int uart_console_write(const char *buf, uint64_t len);
int uart_console_try_getc(char *out);
void uart_console_put_hex_u64(uint64_t value);
void uart_console_put_dec_u32(uint32_t value);
void uart_console_put_dec_u64(uint64_t value);
void uart_console_put_dec_i32(int value);
const char *uart_console_strerror(int err);

#endif
