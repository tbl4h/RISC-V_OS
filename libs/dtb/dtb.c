#include <dtb/dtb.h>
#include <libfdt.h>
#include <string.h>

/*
Globalny, prywatny wskaźnik na blob DTB.
Ustawiany raz przez dtb_init, potem używany przez wszystkie funkcje, żeby nie przekazywać dtb w kółko.
*/
static const void *g_fdt;

/*
Sprawdza, czy g_fdt zostało ustawione.
Jeśli nie, zwraca -FDT_ERR_BADSTATE (standardowy błąd libfdt).
Jeśli tak, zwraca 0.
Używane jako szybki “guard” na wejściu innych funkcji.
*/
static int dtb_require_init(void)
{
    if (!g_fdt)
        return -FDT_ERR_BADSTATE;
    return 0;
}

/*
Pomocnicza funkcja do złożenia wartości 64‑bitowej z tablicy komórek 32‑bitowych (format DTB).
count to liczba komórek (1 albo 2). W DTB reg może mieć 1 lub 2 komórki na adres/rozmiar.
Działa tak:
Sprawdza, czy out jest nie‑NULL i count w zakresie 1..2.
Składa wynik: val = (val << 32) | fdt32_to_cpu(cells[i]).
Zapisuje wynik do *out.
Zwraca 0 albo -FDT_ERR_BADNCELLS
*/
static int read_cells_u64(const fdt32_t *cells, int count, uint64_t *out)
{
    uint64_t val = 0;
    int i;

    if (!out || count <= 0 || count > 2)
        return -FDT_ERR_BADNCELLS;

    for (i = 0; i < count; i++)
        val = (val << 32) | (uint64_t)fdt32_to_cpu(cells[i]);

    *out = val;
    return 0;
}
/*
Inicjalizacja modułu DTB.
Kroki:
Jeśli dtb == NULL, zwraca -FDT_ERR_BADVALUE.
Wywołuje fdt_check_header(dtb) – sprawdza magię, wersję, spójność.
Jeśli błąd → zwraca kod błędu libfdt.
Jeśli OK → zapisuje g_fdt = dtb i zwraca 0.
To jest obowiązkowy krok przed wszystkimi innymi funkcjami.
*/
int dtb_init(void *dtb)
{
    int err;

    if (!dtb)
        return -FDT_ERR_BADVALUE;

    err = fdt_check_header(dtb);
    if (err)
        return err;

    g_fdt = dtb;
    return 0;
}

/*
Zwraca aktualny wskaźnik na DTB (g_fdt).
Nie robi walidacji, więc jeśli dtb_init nie było wywołane, zwróci NULL.
*/
const void *dtb_get(void)
{
    return g_fdt;
}

/*
Odczytuje pierwszy zakres RAM z węzła /memory.
Kroki:
Sprawdza, czy dtb_init było wywołane (dtb_require_init).
Sprawdza, czy base i size nie są NULL.
Znajduje węzeł /memory przez fdt_path_offset.
Odczytuje #address-cells i #size-cells z węzła root (/), aby wiedzieć, ile 32‑bitowych komórek składa się na adres i rozmiar.
Pobiera właściwość reg z /memory.
Sprawdza, czy długość reg jest wystarczająca.
Składa adres i rozmiar przez read_cells_u64.
Zwraca 0 albo kod błędu (np. z fdt_path_offset, fdt_getprop, albo -FDT_ERR_BADNCELLS).
*/
int dtb_get_memory(uint64_t *base, uint64_t *size)
{
    int err;
    int root;
    int mem;
    int naddr;
    int nsize;
    int len;
    const fdt32_t *reg;

    err = dtb_require_init();
    if (err)
        return err;

    if (!base || !size)
        return -FDT_ERR_BADVALUE;

    root = 0;
    mem = fdt_path_offset(g_fdt, "/memory");
    if (mem < 0)
        return mem;

    naddr = fdt_address_cells(g_fdt, root);
    if (naddr < 0)
        return naddr;
    nsize = fdt_size_cells(g_fdt, root);
    if (nsize < 0)
        return nsize;

    if (naddr > 2 || nsize > 2)
        return -FDT_ERR_BADNCELLS;

    reg = fdt_getprop(g_fdt, mem, "reg", &len);
    if (!reg)
        return len;

    if (len < (int)((naddr + nsize) * sizeof(fdt32_t)))
        return -FDT_ERR_BADVALUE;

    err = read_cells_u64(reg, naddr, base);
    if (err)
        return err;
    reg += naddr;
    return read_cells_u64(reg, nsize, size);
}

/*
Czyta timebase-frequency z węzła /cpus.
Kroki:
Wymaga inicjalizacji (dtb_require_init).
Sprawdza, czy timebase != NULL.
Znajduje /cpus.
Pobiera timebase-frequency przez fdt_getprop.
Sprawdza długość i zapisuje wartość do *timebase (fdt32_to_cpu).
Zwraca 0 albo kod błędu.s
*/
int dtb_get_timebase(uint32_t *timebase)
{
    int err;
    int cpus;
    int len;
    const fdt32_t *prop;

    err = dtb_require_init();
    if (err)
        return err;

    if (!timebase)
        return -FDT_ERR_BADVALUE;

    cpus = fdt_path_offset(g_fdt, "/cpus");
    if (cpus < 0)
        return cpus;

    prop = fdt_getprop(g_fdt, cpus, "timebase-frequency", &len);
    if (!prop)
        return len;
    if (len < (int)sizeof(fdt32_t))
        return -FDT_ERR_BADVALUE;

    *timebase = fdt32_to_cpu(*prop);
    return 0;
}

/*
Zlicza wszystkie węzły CPU w /cpus.
Kroki:
Wymaga inicjalizacji (dtb_require_init).
Sprawdza, czy count != NULL.
Znajduje /cpus.
Iteruje po subwęzłach fdt_first_subnode / fdt_next_subnode.
Dla każdego węzła:
sprawdza device_type == "cpu",
pomija jeśli status == "disabled".
Zlicza tylko aktywne CPU.
Zwraca błąd, jeśli iteracja zakończyła się kodem innym niż -FDT_ERR_NOTFOUND.
Zwraca 0 i ustawia *count albo kod błędu.
*/
int dtb_get_cpu_count(int *count)
{
    int err;
    int cpus;
    int off;
    int n = 0;

    err = dtb_require_init();
    if (err)
        return err;

    if (!count)
        return -FDT_ERR_BADVALUE;

    cpus = fdt_path_offset(g_fdt, "/cpus");
    if (cpus < 0)
        return cpus;

    for (off = fdt_first_subnode(g_fdt, cpus);
         off >= 0;
         off = fdt_next_subnode(g_fdt, off)) {
        int len;
        const char *dtype;
        const char *status;

        dtype = fdt_getprop(g_fdt, off, "device_type", &len);
        if (!dtype || strcmp(dtype, "cpu") != 0)
            continue;

        status = fdt_getprop(g_fdt, off, "status", &len);
        if (status && strcmp(status, "disabled") == 0)
            continue;

        n++;
    }

    if (off < 0 && off != -FDT_ERR_NOTFOUND)
        return off;

    *count = n;
    return 0;
}
