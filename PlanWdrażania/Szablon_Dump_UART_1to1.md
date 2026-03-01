# Szablon 1:1 pod `uart_console_*` (do implementacji dumpu)

Poniżej jest gotowy szkielet funkcji dumpu, który używa **wyłącznie**:
- `uart_console_puts`
- `uart_console_put_hex_u64`
- `uart_console_put_dec_u32`
- `uart_console_put_dec_i32`

## Założony model danych (minimalny)

```c
typedef struct {
    uint64_t start;      /* włącznie */
    uint64_t end;        /* wyłącznie */
    const char *source;  /* np. "kernel", "dtb-memory", "uart-mmio" */
} mm_region_t;
```

## Szkielet kodu (kolejność logów 1:1)

```c
#include <stdint.h>
#include <uart/uart_console.h>

#define MM_PAGE_SIZE 0x1000ULL

typedef struct {
    uint64_t start;
    uint64_t end;
    const char *source;
} mm_region_t;

static uint32_t mm_pages_u32(uint64_t start, uint64_t end)
{
    uint64_t bytes = (end > start) ? (end - start) : 0;
    return (uint32_t)(bytes / MM_PAGE_SIZE);
}

static void mm_dump_u32(uint32_t v)
{
    uart_console_put_dec_u32(v);
}

static void mm_dump_hex(uint64_t v)
{
    uart_console_put_hex_u64(v);
}

static void mm_dump_range_line(const char *tag, int idx, const mm_region_t *r)
{
    uart_console_puts("[mm] ");
    uart_console_puts(tag);
    uart_console_puts("[");
    uart_console_put_dec_i32(idx);
    uart_console_puts("] ");
    mm_dump_hex(r->start);
    uart_console_puts("..");
    mm_dump_hex(r->end);
    uart_console_puts(" pages=");
    mm_dump_u32(mm_pages_u32(r->start, r->end));
    if (r->source) {
        uart_console_puts(" src=");
        uart_console_puts(r->source);
    }
    uart_console_puts("\n");
}

static uint32_t mm_sum_pages(const mm_region_t *arr, int count)
{
    int i;
    uint32_t sum = 0;
    for (i = 0; i < count; i++)
        sum += mm_pages_u32(arr[i].start, arr[i].end);
    return sum;
}

void mm_dump_map(const mm_region_t *ram, int ram_count,
                 const mm_region_t *res, int res_count,
                 const mm_region_t *free, int free_count,
                 uint64_t first_free_frame,
                 int first_free_ok,
                 int overlap_free_reserved)
{
    int i;
    uint32_t ram_pages;
    uint32_t res_pages;
    uint32_t free_pages;
    int totals_ok;

    if (!uart_console_is_ready())
        return;

    uart_console_puts("[mm] map dump begin\n");
    uart_console_puts("[mm] page_size=");
    mm_dump_hex(MM_PAGE_SIZE);
    uart_console_puts("\n");

    uart_console_puts("[mm] ram_count=");
    uart_console_put_dec_i32(ram_count);
    uart_console_puts("\n");
    for (i = 0; i < ram_count; i++)
        mm_dump_range_line("ram", i, &ram[i]);

    uart_console_puts("[mm] reserved_count=");
    uart_console_put_dec_i32(res_count);
    uart_console_puts("\n");
    for (i = 0; i < res_count; i++)
        mm_dump_range_line("res", i, &res[i]);

    uart_console_puts("[mm] free_count=");
    uart_console_put_dec_i32(free_count);
    uart_console_puts("\n");
    for (i = 0; i < free_count; i++)
        mm_dump_range_line("free", i, &free[i]);

    ram_pages  = mm_sum_pages(ram, ram_count);
    res_pages  = mm_sum_pages(res, res_count);
    free_pages = mm_sum_pages(free, free_count);
    totals_ok = (ram_pages == (uint32_t)(res_pages + free_pages));

    uart_console_puts("[mm] totals: ram_pages=");
    mm_dump_u32(ram_pages);
    uart_console_puts(" reserved_pages=");
    mm_dump_u32(res_pages);
    uart_console_puts(" free_pages=");
    mm_dump_u32(free_pages);
    uart_console_puts("\n");

    uart_console_puts("[mm] check: R == Z + F -> ");
    uart_console_puts(totals_ok ? "OK\n" : "FAIL\n");

    uart_console_puts("[mm] first_free_frame=");
    mm_dump_hex(first_free_frame);
    uart_console_puts(" -> ");
    uart_console_puts(first_free_ok ? "OK\n" : "FAIL\n");

    uart_console_puts("[mm] overlap_free_reserved=");
    uart_console_put_dec_i32(overlap_free_reserved);
    uart_console_puts(" -> ");
    uart_console_puts(overlap_free_reserved ? "FAIL\n" : "OK\n");

    uart_console_puts("[mm] map dump end\n");
}
```

## Checklista wpięcia (krótka)
- Wywołaj `mm_dump_map(...)` po zbudowaniu `RAM/RESERVED/FREE`.
- Przekaż już **znormalizowane** zakresy (`align_down/up` do `0x1000`).
- `first_free_ok` ustaw na 1 tylko jeśli adres należy do któregoś zakresu `FREE`.
- `overlap_free_reserved` ustaw na 1 jeśli wykryłeś choć jedno przecięcie.
- Najpierw uruchom na `qemu-system-riscv64 -machine virt -nographic` i porównaj log z szablonem.
