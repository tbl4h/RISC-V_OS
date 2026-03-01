#include <libfdt.h>
#include <stdint.h>
#include <dtb/dtb.h>
#include <uart/uart_console.h>
#include <memory_map.h>

#define MM_PAGE_SIZE 0x1000ULL
#define MM_MAX_RAM_REGIONS DTB_MAX_MEM_REGIONS
#define MM_REGION_WORKSPACE_CAP (DTB_MAX_MEM_REGIONS * ((DTB_MAX_REGS * 2) + 8))

enum {
    MM_ERR_BADVALUE = -3000,
    MM_ERR_DTB_RAM,
    MM_ERR_DTB_RESERVED,
    MM_ERR_DTB_DEVICE_SCAN,
    MM_ERR_REGION_CAP,
};

extern char _kernel_start[];
extern char _kernel_image_end[];

/*
 * Tablice regionów pamięci (używane przez mm_state_t)
 */
static mm_region_t g_mm_ram[MM_MAX_RAM_REGIONS];
static mm_region_t g_mm_reserved[MM_REGION_WORKSPACE_CAP];
static mm_region_t g_mm_free[MM_REGION_WORKSPACE_CAP];

/*
 * Bufory tymczasowe dla danych z DTB
 */
static dtb_addr_t g_dtb_ram_regions[DTB_MAX_MEM_REGIONS];
static dtb_addr_t g_dtb_reserved_regions[DTB_MAX_MEM_REGIONS];

/* Struktura przechowująca stan mapy pamięci */
static mm_state_t g_mm_state = {
    .ram = g_mm_ram,
    .reserved = g_mm_reserved,
    .free = g_mm_free,
};

/**
 * Wyrównuje wartość w dół do określonej granicy.
 * @param value Wartość do wyrównania
 * @param align Granica wyrównania (musi być potęgą dwójki)
 * @return Wartość wyrównana w dół
 */
static uint64_t mm_align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1ULL);
}

/**
 * Wyrównuje wartość w górę do określonej granicy.
 * @param value Wartość do wyrównania
 * @param align Granica wyrównania (musi być potęgą dwójki)
 * @return Wartość wyrównana w górę
 */
static uint64_t mm_align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

/**
 * Zwraca mniejszą z dwóch wartości uint64.
 * @param a Pierwsza wartość
 * @param b Druga wartość
 * @return Mniejsza wartość
 */
static uint64_t mm_min_u64(uint64_t a, uint64_t b)
{
    return (a < b) ? a : b;
}

/**
 * Zwraca większą z dwóch wartości uint64.
 * @param a Pierwsza wartość
 * @param b Druga wartość
 * @return Większa wartość
 */
static uint64_t mm_max_u64(uint64_t a, uint64_t b)
{
    return (a > b) ? a : b;
}

/**
 * Oblicza liczbę stron w danym zakresie pamięci.
 * @param start Adres początkowy zakresu
 * @param end Adres końcowy zakresu
 * @return Liczba stron w zakresie
 */
static uint32_t mm_pages_u32(uint64_t start, uint64_t end)
{
    uint64_t bytes = (end > start) ? (end - start) : 0;
    return (uint32_t)(bytes / MM_PAGE_SIZE);
}

/**
 * Dodaje nowy region pamięci do tablicy regionów.
 * Automatycznie wyrównuje adresy do rozmiaru strony.
 * @param arr Tablica regionów
 * @param cap Pojemność tablicy
 * @param count Wskaźnik do licznika elementów
 * @param start Adres początkowy regionu
 * @param end Adres końcowy regionu
 * @param pte_flags Flagi uprawnień PTE (R/W/X)
 * @param protect_flags Flagi ochronne (MM_FLAG_*)
 * @param source Źródło regionu (np. "dtb-memory", "kernel")
 * @return 0 jeśli sukces, kod błędu w przeciwnym razie
 */
static int mm_region_add(mm_region_t *arr, int cap, int *count,
                         uint64_t start, uint64_t end, 
                         uint8_t pte_flags, uint8_t protect_flags, const char *source)
{
    start = mm_align_down(start, MM_PAGE_SIZE);
    end = mm_align_up(end, MM_PAGE_SIZE);

    if (end <= start)
        return 0;
    if (!arr || !count || cap <= 0)
        return MM_ERR_BADVALUE;
    if (*count >= cap)
        return MM_ERR_REGION_CAP;

    arr[*count].start = start;
    arr[*count].end = end;
    arr[*count].pte_flags = pte_flags;
    arr[*count].protect_flags = protect_flags;
    arr[*count].source = source;
    (*count)++;

    return 0;
}

/**
 * Sortuje regiony pamięci według adresu początkowego (algorytm插入-sort).
 * @param arr Tablica regionów do posortowania
 * @param count Liczba elementów w tablicy
 */
static void mm_sort_regions(mm_region_t *arr, int count)
{
    int i;

    for (i = 1; i < count; i++) {
        mm_region_t key = arr[i];
        int j = i - 1;

        while (j >= 0 && arr[j].start > key.start) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/**
 * Scala sąsiadujące lub nakładające się regiony pamięci w jeden.
 * @param arr Tablica regionów
 * @param count Liczba elementów w tablicy
 * @return Liczba regionów po scaleniu
 */
static int mm_merge_regions(mm_region_t *arr, int count)
{
    int i;
    int out;
    static const char merged_source[] = "merged";

    if (count <= 1)
        return count;

    mm_sort_regions(arr, count);
    out = 0;

    for (i = 1; i < count; i++) {
        if (arr[i].start <= arr[out].end) {
            if (arr[i].end > arr[out].end)
                arr[out].end = arr[i].end;
            if (arr[i].source != arr[out].source)
                arr[out].source = merged_source;
            continue;
        }

        out++;
        if (out != i)
            arr[out] = arr[i];
    }

    return out + 1;
}

/**
 * Sumuje liczbę stron we wszystkich regionach pamięci.
 * @param arr Tablica regionów
 * @param count Liczba elementów w tablicy
 * @return Suma stron we wszystkich regionach
 */
static uint32_t mm_sum_pages(const mm_region_t *arr, int count)
{
    int i;
    uint32_t sum = 0;

    for (i = 0; i < count; i++)
        sum += mm_pages_u32(arr[i].start, arr[i].end);

    return sum;
}

/**
 * Sumuje strony z regionów, które mieszczą się w regionach RAM.
 * Oblicza ilość pamięci zarezerwowanej wewnątrz RAM.
 * @param regions Tablica regionów do zsumowania
 * @param region_count Liczba regionów
 * @param ram Tablica regionów RAM
 * @param ram_count Liczba regionów RAM
 * @return Suma stron przyciętych do RAM
 */
static uint32_t mm_sum_pages_clipped_to_ram(const mm_region_t *regions, int region_count,
                                            const mm_region_t *ram, int ram_count)
{
    int i;
    int j;
    uint32_t sum = 0;

    for (i = 0; i < region_count; i++) {
        for (j = 0; j < ram_count; j++) {
            uint64_t start = mm_max_u64(regions[i].start, ram[j].start);
            uint64_t end = mm_min_u64(regions[i].end, ram[j].end);

            if (end > start)
                sum += mm_pages_u32(start, end);
        }
    }

    return sum;
}

/**
 * Sprawdza czy dwa zakresy adresów się nakładają.
 * @param a_start Początek pierwszego zakresu
 * @param a_end Koniec pierwszego zakresu
 * @param b_start Początek drugiego zakresu
 * @param b_end Koniec drugiego zakresu
 * @return 1 jeśli nakładają się, 0 w przeciwnym razie
 */
static int mm_ranges_overlap(uint64_t a_start, uint64_t a_end,
                             uint64_t b_start, uint64_t b_end)
{
    return a_start < b_end && b_start < a_end;
}

/**
 * Sprawdza czy dany zakres nakłada się z którymkolwiek z podanych regionów.
 * @param start Początek sprawdzanego zakresu
 * @param end Koniec sprawdzanego zakresu
 * @param regions Tablica regionów do sprawdzenia
 * @param count Liczba regionów
 * @return 1 jeśli nakłada się z którymkolwiek, 0 w przeciwnym razie
 */
static int mm_region_overlaps_any(uint64_t start, uint64_t end,
                                  const mm_region_t *regions, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (mm_ranges_overlap(start, end, regions[i].start, regions[i].end))
            return 1;
    }

    return 0;
}

/**
 * Dodaje region MMIO do listy zarezerwowanych regionów.
 * @param reserved Tablica zarezerwowanych regionów
 * @param cap Pojemność tablicy
 * @param reserved_count Wskaźnik do licznika
 * @param start Adres początkowy
 * @param size Rozmiar regionu
 * @param source Źródło regionu
 * @return 0 jeśli sukces, kod błędu w przeciwnym razie
 */
static int mm_add_mmio_candidate(mm_region_t *reserved, int cap, int *reserved_count,
                                 uint64_t start, uint64_t size, const char *source)
{
    uint64_t end;

    if (!size)
        return 0;

    end = (size > UINT64_MAX - start) ? UINT64_MAX : start + size;

    return mm_region_add(reserved, cap, reserved_count, start, end, MM_RW, MM_FLAG_MMIO, source);
}

/**
 * Zbiera regiony MMIO z Device Tree Blob (DTB).
 * Przeszukuje wszystkie urządzenia i ich rejestry, dodając te które nie mieszczą się w RAM.
 * @param reserved Tablica zarezerwowanych regionów
 * @param cap Pojemność tablicy
 * @param reserved_count Wskaźnik do licznika
 * @param ram Tablica regionów RAM
 * @param ram_count Liczba regionów RAM
 * @return 0 jeśli sukces, kod błędu w przeciwnym razie
 */
static int mm_collect_dtb_mmio_regions(mm_region_t *reserved, int cap, int *reserved_count,
                                       const mm_region_t *ram, int ram_count)
{
    const void *fdt = dtb_get();
    int depth = -1;
    int node = -1;

    (void)ram;
    (void)ram_count;

    if (!fdt)
        return -FDT_ERR_BADSTATE;

    node = fdt_next_node(fdt, -1, &depth);
    while (node >= 0) {
        int len;
        int reg_idx;
        const char *compatible = fdt_getprop(fdt, node, "compatible", &len);

        if (compatible && len > 0 && compatible[0] != '\0') {
            for (reg_idx = 0; reg_idx < DTB_MAX_REGS; reg_idx++) {
                uint64_t base;
                uint64_t size;
                uint64_t end;
                int reg_err = dtb_decode_reg(node, reg_idx, &base, &size);

                if (reg_err == -FDT_ERR_NOTFOUND)
                    break;
                if (reg_err)
                    continue;
                if (!size)
                    continue;

                end = (size > UINT64_MAX - base) ? UINT64_MAX : base + size;

                if (mm_region_overlaps_any(base, end, ram, ram_count))
                    continue;

                if (mm_add_mmio_candidate(reserved, cap, reserved_count,
                                          base, size, compatible)) {
                    return MM_ERR_REGION_CAP;
                }
            }
        }

        node = fdt_next_node(fdt, node, &depth);
    }

    return 0;
}

/**
 * Sprawdza czy wolne regiony nakładają się z zarezerwowanymi.
 * @param free_regions Tablica wolnych regionów
 * @param free_count Liczba wolnych regionów
 * @param reserved Tablica zarezerwowanych regionów
 * @param reserved_count Liczba zarezerwowanych regionów
 * @return 1 jeśli występuje nakładanie, 0 w przeciwnym razie
 */
static int mm_free_overlaps_reserved(const mm_region_t *free_regions, int free_count,
                                     const mm_region_t *reserved, int reserved_count)
{
    int i;
    int j;

    for (i = 0; i < free_count; i++) {
        for (j = 0; j < reserved_count; j++) {
            if (mm_ranges_overlap(free_regions[i].start, free_regions[i].end,
                                  reserved[j].start, reserved[j].end)) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * Sprawdza czy dany adres mieści się w jednym z podanych regionów.
 * @param addr Adres do sprawdzenia
 * @param regions Tablica regionów
 * @param count Liczba regionów
 * @return 1 jeśli adres jest w regionie, 0 w przeciwnym razie
 */
static int mm_addr_in_regions(uint64_t addr, const mm_region_t *regions, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (regions[i].start <= addr && addr < regions[i].end)
            return 1;
    }

    return 0;
}

/**
 * Konwertuje flagi PTE na tekstowy opis.
 */
static void mm_dump_pte_flags(uint8_t flags)
{
    if (flags & PTE_R)
        uart_console_putc('R');
    else
        uart_console_putc('-');
    
    if (flags & PTE_W)
        uart_console_putc('W');
    else
        uart_console_putc('-');
    
    if (flags & PTE_X)
        uart_console_putc('X');
    else
        uart_console_putc('-');
}

/**
 * Konwertuje flagi ochronne na tekstowy opis.
 */
static void mm_dump_prot_flags(uint8_t flags)
{
    if (flags & MM_FLAG_ALLOCATABLE)
        uart_console_puts("ALLOC");
    else if (flags & MM_FLAG_RESERVED)
        uart_console_puts("RSV");
    
    if (flags & MM_FLAG_KERNEL)
        uart_console_puts("|KERN");
    if (flags & MM_FLAG_BOOT)
        uart_console_puts("|BOOT");
    if (flags & MM_FLAG_MMIO)
        uart_console_puts("|MMIO");
    if (flags & MM_FLAG_DTB)
        uart_console_puts("|DTB");
}

/**
 * Wyświetla pojedynczą linię informacji o zakresie pamięci przez UART.
 * @param tag Etykieta typu regionu (np. "ram", "res", "free")
 * @param idx Indeks regionu
 * @param r Wskaźnik do struktury regionu
 */
static void mm_dump_range_line(const char *tag, int idx, const mm_region_t *r)
{
    uart_console_puts("[mm] ");
    uart_console_puts(tag);
    uart_console_puts("[");
    uart_console_put_dec_i32(idx);
    uart_console_puts("] ");
    uart_console_put_hex_u64(r->start);
    uart_console_puts("..");
    uart_console_put_hex_u64(r->end);
    uart_console_puts(" pages=");
    uart_console_put_dec_u32(mm_pages_u32(r->start, r->end));
    uart_console_puts(" pte=");
    mm_dump_pte_flags(r->pte_flags);
    uart_console_puts("(");
    uart_console_put_hex_u64(r->pte_flags);
    uart_console_puts(") prot=");
    mm_dump_prot_flags(r->protect_flags);
    if (r->source) {
        uart_console_puts(" src=");
        uart_console_puts(r->source);
    }
    uart_console_puts("\n");
}

/**
 * Buduje mapę pamięci systemu i zapisuje wynik do zmiennych globalnych.
 * 
 * Proces:
 * 1. Pobiera regiony RAM z DTB
 * 2. Dodaje regiony zarezerwowane (kernel, boot, DTB, urządzenia)
 * 3. Oblicza wolne regiony jako RAM minus zarezerwowane
 * 4. Weryfikuje poprawność mapy pamięci
 * 
 * @param hw Wskaźnik do struktury stanu sprzętowego
 * @return 0 jeśli sukces, kod błędu w przeciwnym razie
 */
int mm_stage2_build(const hw_state_t *hw)
{
    mm_region_t *ram = g_mm_ram;
    mm_region_t *reserved = g_mm_reserved;
    mm_region_t *free_regions = g_mm_free;
    dtb_addr_t *dtb_ram = g_dtb_ram_regions;
    dtb_addr_t *dtb_reserved = g_dtb_reserved_regions;
    int ram_count = 0;
    int reserved_count = 0;
    int free_count = 0;
    int dtb_ram_count = 0;
    int dtb_reserved_count = 0;
    int i;
    int err;
    uint64_t first_free_frame;
    uint32_t ram_pages;
    uint32_t reserved_pages;
    uint32_t reserved_pages_in_ram;
    uint32_t free_pages;
    int totals_ok;
    int first_free_ok;
    int overlap_free_reserved;
    const void *fdt;

    if (!hw)
        return MM_ERR_BADVALUE;

    err = dtb_memory_regions(dtb_ram, DTB_MAX_MEM_REGIONS, &dtb_ram_count);
    if (err)
        return MM_ERR_DTB_RAM;

    for (i = 0; i < dtb_ram_count; i++) {
        err = mm_region_add(ram, MM_MAX_RAM_REGIONS, &ram_count,
                            dtb_ram[i].base, dtb_ram[i].base + dtb_ram[i].size,
                            MM_RW, MM_FLAG_ALLOCATABLE, "dtb-memory");
        if (err)
            return err;
    }

    ram_count = mm_merge_regions(ram, ram_count);

    first_free_frame = mm_align_up((uint64_t)(uintptr_t)_kernel_image_end, MM_PAGE_SIZE);

    err = mm_region_add(reserved, MM_REGION_WORKSPACE_CAP, &reserved_count,
                        (uint64_t)(uintptr_t)_kernel_start,
                        (uint64_t)(uintptr_t)_kernel_image_end,
                        MM_RWX, MM_FLAG_KERNEL | MM_FLAG_RESERVED, "kernel");
    if (err)
        return err;

    err = mm_region_add(reserved, MM_REGION_WORKSPACE_CAP, &reserved_count,
                        hw->mem_base, first_free_frame,
                        MM_R, MM_FLAG_BOOT | MM_FLAG_RESERVED, "boot-reserved");
    if (err)
        return err;

    fdt = dtb_get();
    if (fdt && fdt_totalsize(fdt) > 0) {
        uint64_t dtb_start = (uint64_t)(uintptr_t)fdt;
        uint64_t dtb_end = dtb_start + (uint64_t)fdt_totalsize(fdt);
        err = mm_region_add(reserved, MM_REGION_WORKSPACE_CAP, &reserved_count,
                            dtb_start, dtb_end,
                            MM_R, MM_FLAG_DTB | MM_FLAG_RESERVED, "dtb");
        if (err)
            return err;
    }

    err = dtb_reserved_memory_regions(dtb_reserved, DTB_MAX_MEM_REGIONS, &dtb_reserved_count);
    if (!err || err == -FDT_ERR_NOSPACE) {
        for (i = 0; i < dtb_reserved_count; i++) {
            err = mm_region_add(reserved, MM_REGION_WORKSPACE_CAP, &reserved_count,
                                dtb_reserved[i].base,
                                dtb_reserved[i].base + dtb_reserved[i].size,
                                MM_R, MM_FLAG_RESERVED, "dtb-reserved");
            if (err)
                return err;
        }
    } else if (err != -FDT_ERR_NOTFOUND) {
        return MM_ERR_DTB_RESERVED;
    }

    err = mm_collect_dtb_mmio_regions(reserved, MM_REGION_WORKSPACE_CAP, &reserved_count,
                                      ram, ram_count);
    if (err)
        return MM_ERR_DTB_DEVICE_SCAN;

    reserved_count = mm_merge_regions(reserved, reserved_count);

    for (i = 0; i < ram_count; i++) {
        uint64_t cursor = ram[i].start;
        int j;

        for (j = 0; j < reserved_count; j++) {
            if (reserved[j].end <= cursor)
                continue;
            if (reserved[j].start >= ram[i].end)
                break;

            if (reserved[j].start > cursor) {
                err = mm_region_add(free_regions, MM_REGION_WORKSPACE_CAP, &free_count,
                                    cursor, mm_min_u64(reserved[j].start, ram[i].end),
                                    MM_RW, MM_FLAG_ALLOCATABLE, "free");
                if (err)
                    return err;
            }

            if (reserved[j].end > cursor)
                cursor = reserved[j].end;

            if (cursor >= ram[i].end)
                break;
        }

        if (cursor < ram[i].end) {
            err = mm_region_add(free_regions, MM_REGION_WORKSPACE_CAP, &free_count,
                                cursor, ram[i].end,
                                MM_RW, MM_FLAG_ALLOCATABLE, "free");
            if (err)
                return err;
        }
    }

    free_count = mm_merge_regions(free_regions, free_count);

    ram_pages = mm_sum_pages(ram, ram_count);
    reserved_pages = mm_sum_pages(reserved, reserved_count);
    reserved_pages_in_ram = mm_sum_pages_clipped_to_ram(reserved, reserved_count, ram, ram_count);
    free_pages = mm_sum_pages(free_regions, free_count);

    totals_ok = (ram_pages == (uint32_t)(reserved_pages_in_ram + free_pages));
    first_free_ok = mm_addr_in_regions(first_free_frame, free_regions, free_count);
    overlap_free_reserved = mm_free_overlaps_reserved(free_regions, free_count,
                                                      reserved, reserved_count);

    /* Zapis do struktury stanu mapy pamięci */
    g_mm_state.ram_count = ram_count;
    g_mm_state.reserved_count = reserved_count;
    g_mm_state.free_count = free_count;
    g_mm_state.first_free_frame = first_free_frame;
    g_mm_state.ram_pages = ram_pages;
    g_mm_state.reserved_pages = reserved_pages;
    g_mm_state.reserved_pages_in_ram = reserved_pages_in_ram;
    g_mm_state.free_pages = free_pages;
    g_mm_state.totals_ok = totals_ok;
    g_mm_state.first_free_ok = first_free_ok;
    g_mm_state.overlap_free_reserved = overlap_free_reserved;

    return 0;
}

/**
 * Wyświetla (dump) mapę pamięci systemu na UART.
 * Funkcja używa danych zapisanych przez mm_stage2_build().
 * 
 * @return 0 jeśli sukces, kod błędu w przeciwnym razie
 */
int mm_stage2_dump(void)
{
    mm_region_t *ram = g_mm_ram;
    mm_region_t *reserved = g_mm_reserved;
    mm_region_t *free_regions = g_mm_free;
    int ram_count = g_mm_state.ram_count;
    int reserved_count = g_mm_state.reserved_count;
    int free_count = g_mm_state.free_count;
    int i;

    if (!uart_console_is_ready())
        return 0;

    uart_console_puts("[mm] map dump begin\n");
    uart_console_puts("[mm] page_size=");
    uart_console_put_hex_u64(MM_PAGE_SIZE);
    uart_console_puts("\n");

    uart_console_puts("[mm] ram_count=");
    uart_console_put_dec_i32(ram_count);
    uart_console_puts("\n");
    for (i = 0; i < ram_count; i++)
        mm_dump_range_line("ram", i, &ram[i]);

    uart_console_puts("[mm] reserved_count=");
    uart_console_put_dec_i32(reserved_count);
    uart_console_puts("\n");
    for (i = 0; i < reserved_count; i++)
        mm_dump_range_line("res", i, &reserved[i]);

    uart_console_puts("[mm] free_count=");
    uart_console_put_dec_i32(free_count);
    uart_console_puts("\n");
    for (i = 0; i < free_count; i++)
        mm_dump_range_line("free", i, &free_regions[i]);

    uart_console_puts("[mm] totals: ram_pages=");
    uart_console_put_dec_u32(g_mm_state.ram_pages);
    uart_console_puts(" reserved_pages=");
    uart_console_put_dec_u32(g_mm_state.reserved_pages);
    uart_console_puts(" reserved_pages_in_ram=");
    uart_console_put_dec_u32(g_mm_state.reserved_pages_in_ram);
    uart_console_puts(" free_pages=");
    uart_console_put_dec_u32(g_mm_state.free_pages);
    uart_console_puts("\n");

    uart_console_puts("[mm] check: R == Z + F -> ");
    uart_console_puts(g_mm_state.totals_ok ? "OK\n" : "FAIL\n");

    uart_console_puts("[mm] first_free_frame=");
    uart_console_put_hex_u64(g_mm_state.first_free_frame);
    uart_console_puts(" -> ");
    uart_console_puts(g_mm_state.first_free_ok ? "OK\n" : "FAIL\n");

    uart_console_puts("[mm] overlap_free_reserved=");
    uart_console_put_dec_i32(g_mm_state.overlap_free_reserved);
    uart_console_puts(" -> ");
    uart_console_puts(g_mm_state.overlap_free_reserved ? "FAIL\n" : "OK\n");

    uart_console_puts("[mm] map dump end\n");

    return 0;
}

/**
 * Buduje i wyświetla mapę pamięci systemu.
 * Funkcja wywołuje mm_stage2_build() a następnie mm_stage2_dump().
 * 
 * @param hw Wskaźnik do struktury stanu sprzętowego
 * @return 0 jeśli sukces, kod błędu w przeciwnym razie
 */
int mm_stage2_build_and_dump(const hw_state_t *hw)
{
    int err;
    
    err = mm_stage2_build(hw);
    if (err)
        return err;
    
    return mm_stage2_dump();
}
