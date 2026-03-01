#include <libfdt.h>
#include <dtb.h>
#include <string.h>

static const void *g_fdt;

/*
Weryfikuje, czy moduł DTB został zainicjalizowany przez sprawdzenie globalnego wskaźnika g_fdt.
Zwraca -FDT_ERR_BADSTATE, gdy dtb_init() nie zostało wywołane, czyli nie ma załadowanego DTB.
Zwraca 0, jeśli wskaźnik jest ustawiony i można kontynuować dalsze parsowanie.
Względnie prosta kontrola, którą wywołują wszystkie publiczne funkcje modułu DTB (np. dtb_node_addr_cells, dtb_device_read, dtb_get_cpu_count, dtb_memory_regions itd.), żeby w każdym przypadku najpierw upewnić się, że blob jest dostępny.
Dzięki temu nie trzeba powtarzać tej samej walidacji w wielu miejscach, a wywołania dalej w drzewie mogą zakładać, że g_fdt nie jest NULL.
*/
static int dtb_require_init(void)
{
    if (!g_fdt)
        return -FDT_ERR_BADSTATE;
    return 0;
}

/*
Zwraca mniejszą z dwóch liczb całkowitych a i b.
Używana do ograniczania wyników (np. liczby wpisów DTB) do maksymalnej pojemności bufora.
*/
static int min_int(int a, int b)
{
    return (a < b) ? a : b;
}

/*
Konwertuje listę komórek 32-bitowych z DTB (cells) na jedną 64-bitową wartość.
Wymaga count w zakresie 1–2, bo DTB używa 1 lub 2 komórek na adres/rozmiar; w przeciwnym razie zwraca -FDT_ERR_BADNCELLS.
Składa wartość przez przesunięcie o 32 bity i dodanie kolejnej komórki (z uwzględnieniem endian), zapisuje wynik w *out.
Jest wykorzystywana wszędzie tam, gdzie trzeba złożyć adres/rozmiar z właściwości reg albo ranges: decode_reg_entry_with_parent, decode_reg_list, dtb_get_clock_frequency, dtb_translate_ranges, dtb_cpu_read i podobne helpery przetwarzające pola DTB.
*/
static int read_cells_u64(const fdt32_t *cells, int count, uint64_t *out)
{
    uint64_t val = 0;
    int i;

    if (!cells || !out || count <= 0 || count > 2)
        return -FDT_ERR_BADNCELLS;

    for (i = 0; i < count; i++)
        val = (val << 32) | (uint64_t)fdt32_to_cpu(cells[i]);

    *out = val;
    return 0;
}
/*
Pobiera z DTB wartość 32-bitową właściwości name dla węzła node, poprawnie obsługując big-endian i zwracając -FDT_ERR_BADVALUE przy braku bufora lub zbyt krótkich danych.
Zwraca kod błędu z fdt_getprop jeśli właściwość nie istnieje, co pozwala wyżej położonym helperom rozróżniać brak pola od innych problemów.
Funkcja jest wykorzystywana wewnętrznie przez moduł (np. get_interrupt_parent, dtb_interrupt_controller_read, dtb_detect_*) gdy trzeba odczytać #cells, phandle lub inne pojedyncze liczby bez powielania logiki walidacji.
*/
static int get_u32_prop(int node, const char *name, uint32_t *out)
{
    int len;
    const fdt32_t *p;

    if (!out)
        return -FDT_ERR_BADVALUE;

    p = fdt_getprop(g_fdt, node, name, &len);
    if (!p)
        return len;
    if (len < (int)sizeof(fdt32_t))
        return -FDT_ERR_BADVALUE;

    *out = fdt32_to_cpu(*p);
    return 0;
}

/*
Sprawdza, czy węzeł node posiada właściwość o nazwie name przez wywołanie fdt_getprop i zwrócenie, czy wskaźnik nie jest NULL.
Używana wszędzie tam, gdzie trzeba tylko wiedzieć, czy pole istnieje, bez odczytywania jego wartości: np. node_is_enabled (czy jest status), dtb_interrupt_controller_read (czy jest interrupt-controller), dtb_is_simple_bus (czy jest compatible = "simple-bus"), dtb_bus_info (czy są ranges), dtb_cpu_read (czy istnieje riscv,svinval) i inne helpery, które sterują zachowaniem na podstawie obecności pola.
*/
static int prop_exists(int node, const char *name)
{
    int len;
    return fdt_getprop(g_fdt, node, name, &len) != 0;
}

/*
Sprawdza, czy węzeł DTB (node) ma w liście compatible dokładnie ciąg needle.
Przechodzi po zerowanych ciągach w compatible i porównuje każdy z nich z needle; jeśli znajdzie dopasowanie, zwraca 1, w przeciwnym razie 0.
Funkcja pojawia się wszędzie tam, gdzie trzeba rozpoznać typ urządzenia bez odczytywania pełnej struktury: np. dtb_interrupt_controller_read rozpoznaje PLIC/CLINT/IMSIC, dtb_detect_* szuka kontrolerów przerwań, dtb_is_simple_bus sprawdza "simple-bus", dtb_get_timer_node szuka "riscv,timer", a ogólnie każdy helper, który musi wiedzieć, czy node jest tym konkretnym urządzeniem.
*/
static int compat_has(int node, const char *needle)
{
    int len;
    int off = 0;
    const char *compat;

    if (!needle)
        return 0;
    compat = fdt_getprop(g_fdt, node, "compatible", &len);
    if (!compat || len <= 0)
        return 0;

    /*
        off < len → nie wyjdź poza bufor

        compat[off] → jeśli trafisz na bajt 0, zakończ
    */
    while (off < len && compat[off]) {
        const char *entry = compat + off;
        if (strcmp(entry, needle) == 0)
            return 1;
        off += (int)strlen(entry) + 1;
    }

    return 0;
}

/*
Pobiera pierwszy ciąg w tablicy compatible dla węzła node. Jeśli właściwość nie istnieje albo jest pusta, zwraca 0.
Używana tam, gdzie wystarczy wiedzieć tylko pierwszy identyfikator urządzenia (np. w node_is_device i dtb_device_read, żeby ustawić out->compatible bez przetwarzania całej listy).
*/
static const char *first_compat(int node)
{
    int len;
    const char *compat = fdt_getprop(g_fdt, node, "compatible", &len);

    if (!compat || len <= 0 || compat[0] == '\0')
        return 0;

    return compat;
}

/*
Ładuje listę referencji (phandle + args) z właściwości prop_name w węźle node do bufora arr, konwertując Big-endianowe komórki na uint32_t.
Waliduje wejście (arr, count, cap) i zwraca -FDT_ERR_BADVALUE przy brakujących buforach.
Jeżeli właściwość nie istnieje, uważa to za brak referencji (count=0, sukces). Gdy liczba komórek przekracza cap, wypełnia tylko tyle, ile się mieści, i zwraca -FDT_ERR_NOSPACE.
Służy do czytania list clocks, resets, dmas, gpios w dtb_device_read, co eliminuje powtarzanie tej logiki dla każdej właściwości.
*/
static int read_ref_list(int node, const char *prop_name, uint32_t *arr, int cap, int *count)
{
    int i;
    int len;
    int n;
    const fdt32_t *prop;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    prop = fdt_getprop(g_fdt, node, prop_name, &len);
    if (!prop) {
        if (len == -FDT_ERR_NOTFOUND) {
            *count = 0;
            return 0;
        }
        return len;
    }

    n = len / (int)sizeof(fdt32_t);
    *count = min_int(n, cap);
    for (i = 0; i < *count; i++)
        arr[i] = fdt32_to_cpu(prop[i]);

    if (n > cap)
        return -FDT_ERR_NOSPACE;

    return 0;
}
/*
Wędruje od węzła node w górę drzewa (fdt_parent_offset) i szuka właściwości interrupt-parent.
Jeśli znajdzie wartość, zapisuje ją do *parent i zwraca 0.
Jeśli napotka inny błąd niż brak (-FDT_ERR_NOTFOUND), propaguje go dalej.
Jeżeli żadna instancja nie ma tej właściwości, zwraca -FDT_ERR_NOTFOUND.
Funkcja jest używana np. w dtb_device_read, żeby ustalić kontroler przerwań obsługujący dane urządzenie i odczytać #interrupt-cells.
*/
static int get_interrupt_parent(int node, uint32_t *parent)
{
    int cur = node;

    if (!parent)
        return -FDT_ERR_BADVALUE;

    while (cur >= 0) {
        int ret = get_u32_prop(cur, "interrupt-parent", parent);
        if (!ret)
            return 0;
        if (ret != -FDT_ERR_NOTFOUND)
            return ret;
        cur = fdt_parent_offset(g_fdt, cur);
    }

    return -FDT_ERR_NOTFOUND;
}
/*
Wywołuje fdt_node_offset_by_phandle, żeby znaleźć offset węzła na podstawie phandle.
Zwraca offset (>=0) lub kod błędu libfdt, jeśli phandle nie istnieje.
Używana do lokalizowania kontrolera przerwań (lub innego węzła) po phandle z interrupt-parent, clocks itp.
*/
static int find_node_by_phandle(uint32_t phandle)
{
    return fdt_node_offset_by_phandle(g_fdt, phandle);
}

/*
Znajduje węzeł kontrolera (parent_node) po phandle i odczytuje jego #interrupt-cells, czyli ile komórek opisuje jedno przerwanie.
Jeśli właściwość istnieje, zapisuje tę wartość do *cells. Jeśli jej brak, przyjmuje domyślnie 1.
Zwraca kod błędu z find_node_by_phandle albo -FDT_ERR_BADVALUE, jeśli przekazano NULL, co upraszcza obsługę błędów wyżej.
Używana w dtb_device_read, żeby wiedzieć, jak dzielić tablicę interrupts urządzenia na rekordy zgodne z kontrolerem przerwań.
*/
static int get_interrupt_cells_for_parent(uint32_t parent_phandle, int *cells)
{
    int parent_node;
    uint32_t val;

    if (!cells)
        return -FDT_ERR_BADVALUE;

    parent_node = find_node_by_phandle(parent_phandle);
    if (parent_node < 0)
        return parent_node;

    if (get_u32_prop(parent_node, "#interrupt-cells", &val) == 0) {
        *cells = (int)val;
        return 0;
    }

    *cells = 1;
    return 0;
}
/*
Odczytuje index‑ty wpis z właściwości reg węzła node, interpretując go zgodnie z #address-cells/#size-cells rodzica.
Najpierw sprawdza argumenty i pobiera rodzica (fdt_parent_offset), potem liczbę komórek adresowych i rozmiarowych (domyślnie 0–2 komórek).
Przeskakuje do właściwego wpisu (entry = reg + index * stride), składa adres przez read_cells_u64, a rozmiar tylko jeśli #size-cells > 0 (bieżący CPU ma #size-cells=0, stąd special case).
Zwraca kody błędów libfdt (-FDT_ERR_BADNCELLS, -FDT_ERR_NOTFOUND itd.), co pozwala wyższym helperom obsłużyć albo zgłosić brak właściwości.
Funkcja jest wykorzystywana wszędzie tam, gdzie trzeba odczytać pojedynczy zakres MMIO: decode_reg_list, dtb_device_read, dtb_cpu_read, dtb_uart_ns16550a itd.
*/
static int decode_reg_entry_with_parent(int node, int index, uint64_t *base, uint64_t *size)
{
    // Długość właściwości reg w bajtach, offset rodzica, liczba komórek adresowych 
    // i rozmiarowych, wskaźnik do tablicy reg, wskaźnik do konkretnego wpisu, kod błędu.
    int len;
    int parent;
    int naddr;
    int nsize;
    int stride;
    const fdt32_t *reg;
    const fdt32_t *entry;
    int err;

    // Sprawdź argumenty i pobierz rodzica.
    if (!base || !size || index < 0)
        return -FDT_ERR_BADVALUE;

    // Pobierz offset rodzica, bo #address-cells/#size-cells są zdefiniowane przez rodzica.
    parent = fdt_parent_offset(g_fdt, node);
    if (parent < 0)
        return parent;

    // Pobierz liczbę komórek adresowych i rozmiarowych dla rodzica.
    // Są one używane do interpretacji właściwości "reg" bieżącego węzła.
    naddr = fdt_address_cells(g_fdt, parent);
    if (naddr < 0)
        return naddr;

    // #size-cells może być 0, co oznacza, że rozmiar jest nieokreślony lub nieistotny (np. dla CPU), więc obsłuż to jako specjalny przypadek.
    nsize = fdt_size_cells(g_fdt, parent);
    if (nsize < 0)
        return nsize;

    // Sprawdź poprawność liczby komórek adresowych i rozmiarowych.
    if (naddr <= 0 || naddr > 2 || nsize < 0 || nsize > 2)
        return -FDT_ERR_BADNCELLS;

    // Stride to liczba komórek potrzebnych do opisu jednego zakresu (adres + rozmiar).
    stride = naddr + nsize;
    reg = fdt_getprop(g_fdt, node, "reg", &len);
    if (!reg)
        return len;

    // Sprawdź, czy indeks jest w zakresie właściwości reg.
    if (len < (index + 1) * stride * (int)sizeof(fdt32_t))
        return -FDT_ERR_NOTFOUND;

    // Wskaźnik do konkretnego wpisu w tablicy reg.
    entry = reg + (index * stride);
    err = read_cells_u64(entry, naddr, base);
    if (err)
        return err;

    if (nsize == 0) {
        *size = 0;
        return 0;
    }

    return read_cells_u64(entry + naddr, nsize, size);
}

/*
Parsuje wszystkie wpisy reg (adres + rozmiar) dla węzła node, stosując #address-cells/#size-cells rodzica i zapisując je do tablicy arr.
Waliduje bufor (arr, count, cap) i zwraca -FDT_ERR_BADVALUE przy błędnych argumentach. Jeśli reg nie istnieje, ustawia *count=0 i kończy sukcesem.
Limit cap zostaje użyty przez min_int, aby nie przepełnić bufora, a gdy liczba wpisów przekracza cap, funkcja zwraca -FDT_ERR_NOSPACE.
decode_reg_entry_with_parent jest wywoływana dla każdego wpisu — dzięki temu można odczytać wszystkie regiony MMIO jednego urządzenia (dtb_device_read), listę pamięci (dtb_memory_regions) czy listę regionów w /reserved-memory.
*/
static int decode_reg_list(int node, dtb_addr_t *arr, int cap, int *count)
{
    int len;
    int parent;
    int naddr;
    int nsize;
    int stride;
    int entries;
    int i;
    int outc;
    const fdt32_t *reg;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    parent = fdt_parent_offset(g_fdt, node);
    if (parent < 0)
        return parent;

    naddr = fdt_address_cells(g_fdt, parent);
    if (naddr < 0)
        return naddr;

    nsize = fdt_size_cells(g_fdt, parent);
    if (nsize < 0)
        return nsize;

    if (naddr <= 0 || naddr > 2 || nsize < 0 || nsize > 2)
        return -FDT_ERR_BADNCELLS;

    stride = naddr + nsize;
    reg = fdt_getprop(g_fdt, node, "reg", &len);
    if (!reg) {
        if (len == -FDT_ERR_NOTFOUND) {
            *count = 0;
            return 0;
        }
        return len;
    }

    entries = len / (stride * (int)sizeof(fdt32_t));
    outc = min_int(entries, cap);

    for (i = 0; i < outc; i++) {
        int err = decode_reg_entry_with_parent(node, i, &arr[i].base, &arr[i].size);
        if (err)
            return err;
    }

    *count = outc;

    if (entries > cap)
        return -FDT_ERR_NOSPACE;

    return 0;
}

/*
Sprawdza, czy węzeł ma właściwość device_type równą "cpu", przy pomocy strcmp.
Pomaga identyfikować węzły procesorów podczas iteracji po /cpus w funkcjach takich jak dtb_get_cpu_count, dtb_cpu_list i dtb_cpu_find_hart.
*/
static int node_is_cpu(int node)
{
    int len;
    const char *dtype = fdt_getprop(g_fdt, node, "device_type", &len);
    return dtype && strcmp(dtype, "cpu") == 0;
}

/*
Sprawdza właściwość status i zwraca true jeśli jej nie ma albo nie równa się "disabled".
Używana wszędzie, gdzie trzeba pominąć wyłączone węzły (np. w iteracji /cpus, /reserved-memory, dtb_device_first/next).
*/
static int node_is_enabled(int node)
{
    int len;
    const char *status = fdt_getprop(g_fdt, node, "status", &len);
    return !status || strcmp(status, "disabled") != 0;
}

/*
Zwraca true jeśli węzeł ma jakiś wpis w compatible (first_compat nie zwraca NULL).
Używana przy iteracji po drzewie (dtb_device_first/next), żeby uznać tylko te węzły, które opisują urządzenia (czyli mają identyfikator compatible).
*/
static int node_is_device(int node)
{
    return first_compat(node) != 0;
}

/*
Upewnia się, że przekazany wskaźnik dtb nie jest NULL; jeśli jest, zwraca -FDT_ERR_BADVALUE.
Wywołuje fdt_check_header, żeby sprawdzić nagłówek DTB (magic, wersja, spójność); przy błędzie zwraca kod libfdt.
Jeśli wszystko OK, zapisuje blob do globalnego g_fdt, dzięki czemu reszta modułu może korzystać z DTB przez helper dtb_require_init.
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
Zwraca wskaźnik do globalnego g_fdt, który jest inicjalizowany przez dtb_init.
Używana przez moduły, które chcą bezpośrednio korzystać z libfdt (np. do debugowania).
*/
const void *dtb_get(void)
{
    return g_fdt;
}


/*
Czym jest: dtb_node_addr_cells (w dtb.c (line 305)) wywołuje dtb_require_init(), a następnie pobiera #address-cells dla wskazanego offsetu w drzewie (fdt_address_cells). Zwraca 0 przy powodzeniu lub bezpośredni kod błędu libfdt, jeżeli g_fdt nie został jeszcze ustawiony albo node jest nieprawidłowy.
Kiedy się pojawia: używasz tej funkcji wszędzie tam, gdzie musisz wiedzieć, ile komórek opisuje adresy w reg/ranges (np. dekoder reg, dtb_translate_ranges, dtb_bus_info). Dzięki niej nie powtarzasz walidacji #address-cells i możesz normalizować różne poziomy drzewa.
*/
int dtb_node_addr_cells(int node, int *cells)
{
    int err = dtb_require_init();
    if (err)
        return err;
    if (!cells)
        return -FDT_ERR_BADVALUE;

    *cells = fdt_address_cells(g_fdt, node);
    return (*cells < 0) ? *cells : 0;
}

/*
dtb_node_size_cells (libs/dtb/dtb.c:317) waliduje g_fdt, sprawdza argument cells, a potem odczytuje #size-cells dla węzła node przez fdt_size_cells. Zwraca wartość bezpośrednio lub kod błędu, jeśli node nie istnieje — to helper używany przy dekodowaniu wpisów reg/ranges, żeby wiedzieć, ile komórek poświęcić rozmiarowi.
dtb_decode_reg (libs/dtb/dtb.c:329) jest cienką nakładką, która ponownie sprawdza inicjalizację i wywołuje decode_reg_entry_with_parent; dlatego można z niej korzystać wszędzie, gdzie potrzebujesz konkretnego index‑tego adresu z reg, bez powielania logiki walidacji rodzica czy liczby komórek.
*/
int dtb_node_size_cells(int node, int *cells)
{
    int err = dtb_require_init();
    if (err)
        return err;
    if (!cells)
        return -FDT_ERR_BADVALUE;

    *cells = fdt_size_cells(g_fdt, node);
    return (*cells < 0) ? *cells : 0;
}

/*
Weryfikuje, czy drzewo FDT zostało zainicjalizowane (dtb_require_init), a następnie oddelegowuje do decode_reg_entry_with_parent, która rozkodowuje konkretny wpis z właściwości reg. To oznacza, że w jednym miejscu masz i sprawdzenie inicjalizacji, i obsługę liczby komórek adresowych/rozmiarowych wynikających z hierarchii węzłów.
Przyjmuje node (offset w drzewie), index (zero‑based numer wpisu w reg) oraz wskaźniki base i size, do których zapisuje odpowiednio bazowy adres i długość dekodowanego zakresu. Zwraca 0 przy powodzeniu lub kod błędu z dtb_require_init / decode_reg_entry_with_parent (np. -FDT_ERR_NOTFOUND), jeśli coś nie przejdzie.
Zastosowanie
Używaj tej funkcji wtedy, gdy potrzebujesz konkretnych parametrów adresowych index‑tego wpisu reg, np. przy inicjalizacji urządzeń na magistralach lub przy tworzeniu mapowań adresowych. Dzięki niej nie musisz powtarzać walidacji rodziców ani liczenia #address‑/#size‑cells po każdym razie – wszystkie te sprawdzenia są już w decode_reg_entry_with_parent.
*/
int dtb_decode_reg(int node, int index, uint64_t *base, uint64_t *size)
{
    int err = dtb_require_init();
    if (err)
        return err;
    return decode_reg_entry_with_parent(node, index, base, size);
}

/*
Tłumaczy adres dziecka (child_addr) na adres CPU przez przeszukanie właściwości ranges węzła node.
Pobiera #address-cells dziecka i rodzica oraz #size-cells, żeby wiedzieć, jak podzielić każdy wpis ranges.
Jeśli ranges nie istnieje lub jest puste, przyjmuje mapowanie 1:1 (child_addr = cpu_addr).
Iteruje po wpisach ranges i szuka zakresu, w którym mieści się child_addr; gdy znajdzie, oblicza offset i dodaje go do adresu bazowego rodzica.
Zwraca -FDT_ERR_NOTFOUND, jeśli żaden zakres nie pasuje, lub kody błędów libfdt przy problemach z odczytem właściwości.
Używana przy dekodowaniu adresów urządzeń na magistralach z translacją adresów (np. PCI, simple-bus z ranges).
*/
int dtb_translate_ranges(int node, uint64_t child_addr, uint64_t *cpu_addr)
{
    int err;
    int parent;
    int child_cells;
    int parent_cells;
    int size_cells;
    int len;
    int stride;
    int i;
    int entries;
    const fdt32_t *ranges;

    err = dtb_require_init();
    if (err)
        return err;

    if (!cpu_addr)
        return -FDT_ERR_BADVALUE;

    parent = fdt_parent_offset(g_fdt, node);
    if (parent < 0)
        return parent;

    child_cells = fdt_address_cells(g_fdt, node);
    if (child_cells < 0)
        return child_cells;

    parent_cells = fdt_address_cells(g_fdt, parent);
    if (parent_cells < 0)
        return parent_cells;

    size_cells = fdt_size_cells(g_fdt, node);
    if (size_cells < 0)
        return size_cells;

    if (child_cells <= 0 || child_cells > 2 || parent_cells <= 0 || parent_cells > 2 || size_cells <= 0 || size_cells > 2)
        return -FDT_ERR_BADNCELLS;

    ranges = fdt_getprop(g_fdt, node, "ranges", &len);
    if (!ranges) {
        if (len == -FDT_ERR_NOTFOUND) {
            *cpu_addr = child_addr;
            return 0;
        }
        return len;
    }

    if (len == 0) {
        *cpu_addr = child_addr;
        return 0;
    }

    stride = child_cells + parent_cells + size_cells;
    entries = len / (stride * (int)sizeof(fdt32_t));
    for (i = 0; i < entries; i++) {
        uint64_t cbase;
        uint64_t pbase;
        uint64_t sz;
        uint64_t off;
        const fdt32_t *entry = ranges + (i * stride);

        err = read_cells_u64(entry, child_cells, &cbase);
        if (err)
            return err;
        err = read_cells_u64(entry + child_cells, parent_cells, &pbase);
        if (err)
            return err;
        err = read_cells_u64(entry + child_cells + parent_cells, size_cells, &sz);
        if (err)
            return err;

        if (child_addr >= cbase && child_addr < (cbase + sz)) {
            off = child_addr - cbase;
            *cpu_addr = pbase + off;
            return 0;
        }
    }

    return -FDT_ERR_NOTFOUND;
}

/*
Zwraca offset pierwszego węzła w drzewie DTB, który jest urządzeniem (ma compatible) i jest włączony (status != "disabled").
Iteruje po wszystkich węzłach od korzenia przez fdt_next_node, sprawdzając node_is_device i node_is_enabled.
Zapisuje offset do *node i zwraca 0 przy sukcesie, lub -FDT_ERR_NOTFOUND gdy nie ma żadnego pasującego węzła.
Używana razem z dtb_device_next do iteracji po wszystkich urządzeniach w drzewie.
*/
int dtb_device_first(int *node)
{
    int err;
    int depth = -1;
    int off;

    err = dtb_require_init();
    if (err)
        return err;

    if (!node)
        return -FDT_ERR_BADVALUE;

    off = fdt_next_node(g_fdt, -1, &depth);
    while (off >= 0) {
        if (node_is_device(off) && node_is_enabled(off)) {
            *node = off;
            return 0;
        }
        off = fdt_next_node(g_fdt, off, &depth);
    }

    return -FDT_ERR_NOTFOUND;
}

/*
Zwraca offset kolejnego węzła urządzenia po węźle wskazanym przez *node.
Kontynuuje iterację przez fdt_next_node od bieżącego węzła, szukając następnego, który spełnia node_is_device i node_is_enabled.
Aktualizuje *node i zwraca 0 przy sukcesie, lub -FDT_ERR_NOTFOUND gdy nie ma więcej pasujących węzłów.
Używana w pętli razem z dtb_device_first do przeglądania wszystkich urządzeń w drzewie DTB.
*/
int dtb_device_next(int *node)
{
    int err;
    int depth = 0;
    int off;

    err = dtb_require_init();
    if (err)
        return err;

    if (!node)
        return -FDT_ERR_BADVALUE;

    off = fdt_next_node(g_fdt, *node, &depth);
    while (off >= 0) {
        if (node_is_device(off) && node_is_enabled(off)) {
            *node = off;
            return 0;
        }
        off = fdt_next_node(g_fdt, off, &depth);
    }

    return -FDT_ERR_NOTFOUND;
}

/*
Wypełnia strukturę dtb_device_t danymi z węzła node: nazwę, pierwszy ciąg compatible, listę regionów MMIO (reg), przerwania (interrupts) z uwzględnieniem #interrupt-cells kontrolera nadrzędnego, oraz listy referencji clocks, resets, dmas, gpios.
Zeruje strukturę przed wypełnieniem, żeby nieużywane pola były zawsze zerem.
Przerwania są grupowane według irq_cells pobranego z interrupt-parent; każdy rekord irqs[] zawiera numer przerwania, phandle kontrolera i liczbę komórek.
Zwraca 0 przy sukcesie lub kod błędu libfdt, jeśli inicjalizacja nie była wykonana albo out jest NULL.
Używana przez dtb_interrupt_map_device i bezpośrednio przez kod jądra do odczytu pełnego opisu urządzenia.
*/
int dtb_device_read(int node, dtb_device_t *out)
{
    int err;
    uint32_t parent_phandle = 0;
    int irq_cells = 1;
    int len;
    const fdt32_t *intr;

    err = dtb_require_init();
    if (err)
        return err;

    if (!out)
        return -FDT_ERR_BADVALUE;

    memset(out, 0, sizeof(*out));
    out->node = node;
    out->name = fdt_get_name(g_fdt, node, 0);
    out->compatible = first_compat(node);

    err = decode_reg_list(node, out->regs, DTB_MAX_REGS, &out->reg_count);
    if (err && err != -FDT_ERR_NOSPACE)
        return err;

    if (get_interrupt_parent(node, &parent_phandle) == 0)
        get_interrupt_cells_for_parent(parent_phandle, &irq_cells);

    intr = fdt_getprop(g_fdt, node, "interrupts", &len);
    if (intr && len > 0) {
        int cells = len / (int)sizeof(fdt32_t);
        int groups = (irq_cells > 0) ? (cells / irq_cells) : 0;
        int i;

        out->irq_count = min_int(groups, DTB_MAX_IRQS);
        for (i = 0; i < out->irq_count; i++) {
            out->irqs[i].irq = fdt32_to_cpu(intr[i * irq_cells]);
            out->irqs[i].parent_phandle = parent_phandle;
            out->irqs[i].cells = (uint32_t)irq_cells;
        }
    }

    read_ref_list(node, "clocks", out->clocks, DTB_MAX_REFS, &out->clock_count);
    read_ref_list(node, "resets", out->resets, DTB_MAX_REFS, &out->reset_count);
    read_ref_list(node, "dmas", out->dmas, DTB_MAX_REFS, &out->dma_count);
    read_ref_list(node, "gpios", out->gpios, DTB_MAX_REFS, &out->gpio_count);

    return 0;
}

/*
Wyszukuje pierwszy węzeł w drzewie DTB, którego lista compatible zawiera dokładnie ciąg compat.
Używa fdt_node_offset_by_compatible zaczynając od korzenia (-1).
Zapisuje offset do *node i zwraca 0 przy sukcesie, lub kod błędu libfdt (np. -FDT_ERR_NOTFOUND) gdy węzeł nie istnieje.
Używana do szybkiego lokalizowania konkretnego urządzenia po jego identyfikatorze compatible.
*/
int dtb_find_compatible(const char *compat, int *node)
{
    int err;
    int off;

    err = dtb_require_init();
    if (err)
        return err;

    if (!compat || !node)
        return -FDT_ERR_BADVALUE;

    off = fdt_node_offset_by_compatible(g_fdt, -1, compat);
    if (off < 0)
        return off;

    *node = off;
    return 0;
}

/*
Wyszukuje kolejny węzeł z podanym ciągiem compatible, zaczynając od start_node (a nie od korzenia).
Pozwala iterować po wszystkich węzłach z tym samym compatible przez kolejne wywołania z poprzednio znalezionym offsetem.
Zwraca 0 i zapisuje offset do *node przy sukcesie, lub kod błędu libfdt gdy nie ma więcej pasujących węzłów.
Używana gdy w drzewie może być wiele węzłów tego samego typu (np. wiele UART, wiele bloków pamięci).
*/
int dtb_find_compatible_n(const char *compat, int start_node, int *node)
{
    int err;
    int off;

    err = dtb_require_init();
    if (err)
        return err;

    if (!compat || !node)
        return -FDT_ERR_BADVALUE;

    off = fdt_node_offset_by_compatible(g_fdt, start_node, compat);
    if (off < 0)
        return off;

    *node = off;
    return 0;
}

/*
Odczytuje dane kontrolera przerwań z węzła node do struktury dtb_intc_t.
Sprawdza, czy węzeł ma właściwość interrupt-controller; jeśli nie, zwraca -FDT_ERR_NOTFOUND.
Rozpoznaje typ kontrolera na podstawie compatible: "plic" dla riscv,plic0/sifive,plic-1.0.0, "clint" dla riscv,clint0, "imsic" dla riscv,imsics; nieznane typy dostają "unknown".
Odczytuje phandle, #interrupt-cells i listę regionów MMIO (reg).
Używana przez dtb_interrupt_controllers_scan do budowania tablicy wszystkich kontrolerów przerwań w systemie.
*/
int dtb_interrupt_controller_read(int node, dtb_intc_t *out)
{
    int err;

    err = dtb_require_init();
    if (err)
        return err;

    if (!out)
        return -FDT_ERR_BADVALUE;

    if (!prop_exists(node, "interrupt-controller"))
        return -FDT_ERR_NOTFOUND;

    memset(out, 0, sizeof(*out));
    out->node = node;
    out->type = "unknown";

    if (compat_has(node, "riscv,plic0") || compat_has(node, "sifive,plic-1.0.0"))
        out->type = "plic";
    else if (compat_has(node, "riscv,clint0"))
        out->type = "clint";
    else if (compat_has(node, "riscv,imsics"))
        out->type = "imsic";

    get_u32_prop(node, "phandle", &out->phandle);
    get_u32_prop(node, "#interrupt-cells", &out->interrupt_cells);

    err = decode_reg_list(node, out->regs, DTB_MAX_REGS, &out->reg_count);
    if (err && err != -FDT_ERR_NOSPACE && err != -FDT_ERR_NOTFOUND)
        return err;

    return 0;
}

/*
Skanuje całe drzewo DTB i zbiera wszystkie węzły z właściwością interrupt-controller do tablicy arr.
Iteruje przez fdt_next_node i dla każdego pasującego węzła wywołuje dtb_interrupt_controller_read.
Ogranicza liczbę wpisów do cap przez min_int; gdy jest ich więcej, zwraca -FDT_ERR_NOSPACE.
Używana przy inicjalizacji systemu przerwań, żeby wykryć i skatalogować wszystkie dostępne kontrolery.
*/
int dtb_interrupt_controllers_scan(dtb_intc_t *arr, int cap, int *count)
{
    int err;
    int depth = -1;
    int off;
    int n = 0;

    err = dtb_require_init();
    if (err)
        return err;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    off = fdt_next_node(g_fdt, -1, &depth);
    while (off >= 0) {
        if (prop_exists(off, "interrupt-controller")) {
            if (n < cap)
                dtb_interrupt_controller_read(off, &arr[n]);
            n++;
        }
        off = fdt_next_node(g_fdt, off, &depth);
    }

    *count = min_int(n, cap);
    return (n > cap) ? -FDT_ERR_NOSPACE : 0;
}

/*
Pobiera listę przerwań urządzenia dev_node i kopiuje je do tablicy arr.
Wewnętrznie wywołuje dtb_device_read, żeby uzyskać pełny opis urządzenia z wypełnionymi polami irqs[].
Ogranicza liczbę wpisów do cap; gdy urządzenie ma więcej przerwań, zwraca -FDT_ERR_NOSPACE.
Upraszcza kod wywołujący, który potrzebuje tylko listy przerwań bez pełnej struktury dtb_device_t.
*/
int dtb_interrupt_map_device(int dev_node, dtb_irq_t *arr, int cap, int *count)
{
    dtb_device_t dev;
    int i;
    int n;
    int err;

    err = dtb_require_init();
    if (err)
        return err;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    err = dtb_device_read(dev_node, &dev);
    if (err)
        return err;

    n = min_int(dev.irq_count, cap);
    for (i = 0; i < n; i++)
        arr[i] = dev.irqs[i];

    *count = n;
    return (dev.irq_count > cap) ? -FDT_ERR_NOSPACE : 0;
}

/*
Przeszukuje drzewo DTB w poszukiwaniu pierwszego węzła pasującego do któregokolwiek z ciągów w tablicy compat (o długości compat_count).
Iteruje po liście compatible i wywołuje fdt_node_offset_by_compatible dla każdego; przy pierwszym trafieniu zapisuje offset do *node i zwraca 0.
Jeśli żaden ciąg nie pasuje, zwraca -FDT_ERR_NOTFOUND.
Używana przez dtb_detect_plic, dtb_detect_clint, dtb_detect_imsic i dtb_get_timer_node, żeby obsłużyć wiele możliwych nazw compatible dla tego samego typu urządzenia.
*/
static int find_any_compatible(const char **compat, int compat_count, int *node)
{
    int i;
    for (i = 0; i < compat_count; i++) {
        int off = fdt_node_offset_by_compatible(g_fdt, -1, compat[i]);
        if (off >= 0) {
            *node = off;
            return 0;
        }
    }
    return -FDT_ERR_NOTFOUND;
}

/*
Wykrywa węzeł PLIC (Platform-Level Interrupt Controller) w drzewie DTB, sprawdzając compatible "riscv,plic0" lub "sifive,plic-1.0.0".
Zapisuje offset węzła do *node i zwraca 0 przy sukcesie, lub -FDT_ERR_NOTFOUND gdy PLIC nie istnieje w DTB.
Używana przy inicjalizacji systemu przerwań do lokalizowania kontrolera PLIC.
*/
int dtb_detect_plic(int *node)
{
    const char *compat[] = { "riscv,plic0", "sifive,plic-1.0.0" };
    int err = dtb_require_init();
    if (err)
        return err;
    if (!node)
        return -FDT_ERR_BADVALUE;
    return find_any_compatible(compat, 2, node);
}

/*
Wykrywa węzeł CLINT (Core-Local Interruptor) w drzewie DTB, sprawdzając compatible "riscv,clint0" lub "sifive,clint0".
Zapisuje offset węzła do *node i zwraca 0 przy sukcesie, lub -FDT_ERR_NOTFOUND gdy CLINT nie istnieje w DTB.
Używana przy inicjalizacji timerów i IPI do lokalizowania kontrolera CLINT.
*/
int dtb_detect_clint(int *node)
{
    const char *compat[] = { "riscv,clint0", "sifive,clint0" };
    int err = dtb_require_init();
    if (err)
        return err;
    if (!node)
        return -FDT_ERR_BADVALUE;
    return find_any_compatible(compat, 2, node);
}

/*
Wykrywa węzeł IMSIC (Incoming Message-Signaled Interrupt Controller) w drzewie DTB, sprawdzając compatible "riscv,imsics".
Zapisuje offset węzła do *node i zwraca 0 przy sukcesie, lub -FDT_ERR_NOTFOUND gdy IMSIC nie istnieje w DTB.
Używana przy inicjalizacji systemu przerwań AIA (Advanced Interrupt Architecture) do lokalizowania kontrolera IMSIC.
*/
int dtb_detect_imsic(int *node)
{
    const char *compat[] = { "riscv,imsics" };
    int err = dtb_require_init();
    if (err)
        return err;
    if (!node)
        return -FDT_ERR_BADVALUE;
    return find_any_compatible(compat, 1, node);
}

/*
Odczytuje właściwość timebase-frequency z węzła /cpus w DTB i zapisuje ją do *timebase.
Wartość ta określa częstotliwość zegara czasu rzeczywistego (RTC) używanego przez timer RISC-V.
Zwraca 0 przy sukcesie lub kod błędu libfdt, jeśli węzeł /cpus nie istnieje albo właściwość jest zbyt krótka.
Używana przy inicjalizacji timera do przeliczania ticków na jednostki czasu.
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
Ta funkcja to mały helper do odczytu pojedynczej 32-bitowej właściwości z 
drzewa urządzeń (DTB) dla danego węzła.
Kiedy jest używana:

Gdy masz węzeł DTB i chcesz szybko pobrać jedną prostą wartość typu u32 z jakiejś właściwości, bez pisania za każdym razem tej samej logiki fdt_getprop + długość + konwersja endianness.
W Twoim kernelu używasz jej przy inicjalizacji UART, żeby odczytać np.:
current-speed (baud),
reg-shift,
reg-io-width,
z węzła UART wykrytego w DTB (zob. kernel.c).

*/
int dtb_get_u32(int node, const char *prop_name, uint32_t *out)
{
    int err;
    int len;
    const fdt32_t *prop;

    err = dtb_require_init();
    if (err)
        return err;
    if (!prop_name || !out)
        return -FDT_ERR_BADVALUE;

    prop = fdt_getprop(g_fdt, node, prop_name, &len);
    if (!prop)
        return len;
    if (len < (int)sizeof(fdt32_t))
        return -FDT_ERR_BADVALUE;

    *out = fdt32_to_cpu(*prop);
    return 0;
}

/*
Odczytuje właściwość clock-frequency z węzła node i zapisuje ją jako 64-bitową wartość do *freq.
Obsługuje zarówno 32-bitowe (1 komórka), jak i 64-bitowe (2 komórki) reprezentacje częstotliwości.
Zwraca 0 przy sukcesie lub kod błędu libfdt, jeśli właściwość nie istnieje albo jest zbyt krótka.
Używana do odczytu częstotliwości taktowania urządzeń (np. UART, kontrolerów) z DTB.
*/
int dtb_get_clock_frequency(int node, uint64_t *freq)
{
    int err;
    int len;
    const fdt32_t *prop;

    err = dtb_require_init();
    if (err)
        return err;

    if (!freq)
        return -FDT_ERR_BADVALUE;

    prop = fdt_getprop(g_fdt, node, "clock-frequency", &len);
    if (!prop)
        return len;

    if (len >= (int)(2 * sizeof(fdt32_t)))
        return read_cells_u64(prop, 2, freq);
    if (len >= (int)sizeof(fdt32_t)) {
        *freq = (uint64_t)fdt32_to_cpu(*prop);
        return 0;
    }

    return -FDT_ERR_BADVALUE;
}

/*
Odczytuje listę referencji clocks z węzła node do tablicy clks (maksymalnie cap wpisów).
Jest cienką nakładką na read_ref_list, która dodaje walidację inicjalizacji DTB przez dtb_require_init.
Zwraca 0 przy sukcesie, -FDT_ERR_NOSPACE gdy lista jest dłuższa niż cap, lub kod błędu libfdt.
Używana gdy potrzebna jest tylko lista zegarów urządzenia bez pełnej struktury dtb_device_t.
*/
int dtb_get_device_clocks(int node, uint32_t *clks, int cap, int *count)
{
    int err = dtb_require_init();
    if (err)
        return err;
    return read_ref_list(node, "clocks", clks, cap, count);
}

/*
Wyszukuje węzeł timera RISC-V w drzewie DTB, sprawdzając compatible "riscv,timer" lub "riscv,clint0".
Zapisuje offset węzła do *node i zwraca 0 przy sukcesie, lub -FDT_ERR_NOTFOUND gdy żaden timer nie istnieje.
Używana przy inicjalizacji timera jądra do lokalizowania źródła przerwań czasowych.
*/
int dtb_get_timer_node(int *node)
{
    const char *compat[] = {
        "riscv,timer",
        "riscv,clint0",
        "riscv,aclint-mtimer",
    };
    int err = dtb_require_init();
    if (err)
        return err;
    if (!node)
        return -FDT_ERR_BADVALUE;
    return find_any_compatible(compat, 3, node);
}

/*
Zlicza aktywne węzły CPU w węźle /cpus drzewa DTB.
Iteruje po podwęzłach /cpus, sprawdzając node_is_cpu (device_type = "cpu") i node_is_enabled (status != "disabled").
Zapisuje liczbę aktywnych procesorów do *count i zwraca 0 przy sukcesie.
Używana przy inicjalizacji SMP do ustalenia liczby dostępnych rdzeni.
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
        if (!node_is_cpu(off))
            continue;
        if (!node_is_enabled(off))
            continue;
        n++;
    }

    if (off < 0 && off != -FDT_ERR_NOTFOUND)
        return off;

    *count = n;
    return 0;
}

/*
Wypełnia strukturę dtb_cpu_t danymi z węzła CPU: hartid (z reg), ciągi isa, mmu-type, riscv,isa, flagę svinval (obecność riscv,svinval) i status.
Sprawdza, czy cpu_node jest faktycznie węzłem CPU przez node_is_cpu; jeśli nie, zwraca -FDT_ERR_BADVALUE.
Jeśli status nie istnieje, przyjmuje domyślnie "okay".
Używana przez dtb_cpu_list i bezpośrednio przez kod jądra do odczytu parametrów konkretnego rdzenia.
*/
int dtb_cpu_read(int cpu_node, dtb_cpu_t *out)
{
    int err;
    uint64_t hart;
    uint64_t dummy_size;
    int len;

    err = dtb_require_init();
    if (err)
        return err;

    if (!out)
        return -FDT_ERR_BADVALUE;
    if (!node_is_cpu(cpu_node))
        return -FDT_ERR_BADVALUE;

    memset(out, 0, sizeof(*out));

    err = decode_reg_entry_with_parent(cpu_node, 0, &hart, &dummy_size);
    if (!err)
        out->hartid = (uint32_t)hart;

    out->isa = fdt_getprop(g_fdt, cpu_node, "isa", &len);
    out->mmu_type = fdt_getprop(g_fdt, cpu_node, "mmu-type", &len);
    out->riscv_isa = fdt_getprop(g_fdt, cpu_node, "riscv,isa", &len);
    out->svinval = prop_exists(cpu_node, "riscv,svinval") ? 1 : 0;
    out->status = fdt_getprop(g_fdt, cpu_node, "status", &len);
    if (!out->status)
        out->status = "okay";

    return 0;
}

/*
Buduje tablicę dtb_cpu_t dla wszystkich aktywnych węzłów CPU w /cpus.
Iteruje po podwęzłach /cpus, filtrując przez node_is_cpu i node_is_enabled, i wywołuje dtb_cpu_read dla każdego.
Ogranicza liczbę wpisów do cap; gdy jest ich więcej, zwraca -FDT_ERR_NOSPACE.
Używana przy inicjalizacji SMP do zebrania pełnych informacji o wszystkich dostępnych rdzeniach.
*/
int dtb_cpu_list(dtb_cpu_t *arr, int cap, int *count)
{
    int err;
    int cpus;
    int off;
    int n = 0;

    err = dtb_require_init();
    if (err)
        return err;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    cpus = fdt_path_offset(g_fdt, "/cpus");
    if (cpus < 0)
        return cpus;

    for (off = fdt_first_subnode(g_fdt, cpus);
         off >= 0;
         off = fdt_next_subnode(g_fdt, off)) {
        if (!node_is_cpu(off))
            continue;
        if (!node_is_enabled(off))
            continue;

        if (n < cap)
            dtb_cpu_read(off, &arr[n]);
        n++;
    }

    if (off < 0 && off != -FDT_ERR_NOTFOUND)
        return off;

    *count = min_int(n, cap);
    return (n > cap) ? -FDT_ERR_NOSPACE : 0;
}

/*
Wyszukuje węzeł CPU o podanym hartid w węźle /cpus drzewa DTB.
Iteruje po podwęzłach /cpus, sprawdzając node_is_cpu i odczytując reg przez decode_reg_entry_with_parent; gdy reg pasuje do hartid, zapisuje offset do *cpu_node.
Zwraca 0 przy sukcesie lub -FDT_ERR_NOTFOUND gdy żaden węzeł nie ma podanego hartid.
Używana do mapowania identyfikatora hart na węzeł DTB, np. przy inicjalizacji konkretnego rdzenia.
*/
int dtb_cpu_find_hart(uint32_t hartid, int *cpu_node)
{
    int err;
    int cpus;
    int off;

    err = dtb_require_init();
    if (err)
        return err;

    if (!cpu_node)
        return -FDT_ERR_BADVALUE;

    cpus = fdt_path_offset(g_fdt, "/cpus");
    if (cpus < 0)
        return cpus;

    for (off = fdt_first_subnode(g_fdt, cpus);
         off >= 0;
         off = fdt_next_subnode(g_fdt, off)) {
        uint64_t reg;
        uint64_t dummy_size;

        if (!node_is_cpu(off))
            continue;

        if (!decode_reg_entry_with_parent(off, 0, &reg, &dummy_size) && (uint32_t)reg == hartid) {
            *cpu_node = off;
            return 0;
        }
    }

    return -FDT_ERR_NOTFOUND;
}

/*
Zwraca adres bazowy i rozmiar pierwszego regionu pamięci z DTB.
Wewnętrznie wywołuje dtb_memory_regions i bierze pierwszy wpis z tablicy wyników.
Zwraca -FDT_ERR_NOTFOUND gdy nie ma żadnego regionu pamięci, lub kod błędu libfdt przy problemach z odczytem.
Używana jako uproszczony interfejs gdy system ma jeden ciągły region RAM i nie potrzebuje pełnej listy.
*/
int dtb_get_memory(uint64_t *base, uint64_t *size)
{
    dtb_addr_t regions[DTB_MAX_MEM_REGIONS];
    int count;
    int err;

    err = dtb_require_init();
    if (err)
        return err;

    if (!base || !size)
        return -FDT_ERR_BADVALUE;

    err = dtb_memory_regions(regions, DTB_MAX_MEM_REGIONS, &count);
    if (err)
        return err;

    if (count <= 0)
        return -FDT_ERR_NOTFOUND;

    *base = regions[0].base;
    *size = regions[0].size;
    return 0;
}

/*
Zbiera wszystkie regiony pamięci z węzłów o device_type = "memory" w drzewie DTB.
Iteruje przez fdt_next_node, szukając węzłów pamięci, i dla każdego wywołuje decode_reg_list, żeby odczytać wszystkie zakresy adresowe.
Ogranicza liczbę wpisów do cap; gdy jest ich więcej, zwraca -FDT_ERR_NOSPACE.
Używana przez dtb_get_memory i dtb_memory_total, a także bezpośrednio przez kod zarządzania pamięcią.
*/
int dtb_memory_regions(dtb_addr_t *arr, int cap, int *count)
{
    int err;
    int depth = -1;
    int off;
    int n = 0;

    err = dtb_require_init();
    if (err)
        return err;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    off = fdt_next_node(g_fdt, -1, &depth);
    while (off >= 0) {
        int len;
        const char *dtype = fdt_getprop(g_fdt, off, "device_type", &len);
        if (dtype && strcmp(dtype, "memory") == 0) {
            dtb_addr_t regs[DTB_MAX_REGS];
            int reg_count = 0;
            int i;
            int ret = decode_reg_list(off, regs, DTB_MAX_REGS, &reg_count);
            if (ret && ret != -FDT_ERR_NOSPACE)
                return ret;

            for (i = 0; i < reg_count; i++) {
                if (n < cap)
                    arr[n] = regs[i];
                n++;
            }
        }
        off = fdt_next_node(g_fdt, off, &depth);
    }

    *count = min_int(n, cap);
    return (n > cap) ? -FDT_ERR_NOSPACE : 0;
}

/*
Zbiera wszystkie regiony zarezerwowanej pamięci z podwęzłów /reserved-memory w drzewie DTB.
Iteruje po podwęzłach /reserved-memory i dla każdego wywołuje decode_reg_list, żeby odczytać zakresy adresowe.
Ignoruje błędy -FDT_ERR_NOSPACE i -FDT_ERR_NOTFOUND dla poszczególnych podwęzłów, żeby kontynuować iterację.
Ogranicza liczbę wpisów do cap; gdy jest ich więcej, zwraca -FDT_ERR_NOSPACE.
Używana przy inicjalizacji zarządzania pamięcią do wykluczenia zarezerwowanych obszarów z alokacji.
*/
int dtb_reserved_memory_regions(dtb_addr_t *arr, int cap, int *count)
{
    int err;
    int rmem;
    int sub;
    int n = 0;

    err = dtb_require_init();
    if (err)
        return err;

    if (!arr || !count || cap < 0)
        return -FDT_ERR_BADVALUE;

    rmem = fdt_path_offset(g_fdt, "/reserved-memory");
    if (rmem < 0)
        return rmem;

    for (sub = fdt_first_subnode(g_fdt, rmem);
         sub >= 0;
         sub = fdt_next_subnode(g_fdt, sub)) {
        dtb_addr_t regs[DTB_MAX_REGS];
        int reg_count = 0;
        int i;
        int ret = decode_reg_list(sub, regs, DTB_MAX_REGS, &reg_count);
        if (ret && ret != -FDT_ERR_NOSPACE && ret != -FDT_ERR_NOTFOUND)
            return ret;

        for (i = 0; i < reg_count; i++) {
            if (n < cap)
                arr[n] = regs[i];
            n++;
        }
    }

    *count = min_int(n, cap);
    return (n > cap) ? -FDT_ERR_NOSPACE : 0;
}

/*
Oblicza łączny rozmiar wszystkich regionów pamięci w DTB przez zsumowanie pól size z tablicy zwróconej przez dtb_memory_regions.
Zapisuje wynik do *bytes i zwraca 0 przy sukcesie lub kod błędu libfdt.
Używana do szybkiego sprawdzenia całkowitej ilości dostępnej pamięci RAM bez iterowania po regionach.
*/
int dtb_memory_total(uint64_t *bytes)
{
    dtb_addr_t regs[DTB_MAX_MEM_REGIONS];
    int count;
    int i;
    int err;

    err = dtb_require_init();
    if (err)
        return err;

    if (!bytes)
        return -FDT_ERR_BADVALUE;

    err = dtb_memory_regions(regs, DTB_MAX_MEM_REGIONS, &count);
    if (err)
        return err;

    *bytes = 0;
    for (i = 0; i < count; i++)
        *bytes += regs[i].size;

    return 0;
}

/*
Wyszukuje węzeł UART NS16550A w drzewie DTB, sprawdzając compatible "ns16550a" lub "ns16550".
Odczytuje adres bazowy i rozmiar pierwszego regionu MMIO przez decode_reg_entry_with_parent.
Zapisuje offset węzła do *node, adres do *base i rozmiar do *size; zwraca 0 przy sukcesie.
Używana przy wczesnej inicjalizacji konsoli do lokalizowania portu szeregowego dla debugowania.
*/
int dtb_uart_ns16550a(int *node, uint64_t *base, uint64_t *size)
{
    int err;
    int uart;

    err = dtb_require_init();
    if (err)
        return err;

    if (!node || !base || !size)
        return -FDT_ERR_BADVALUE;

    uart = fdt_node_offset_by_compatible(g_fdt, -1, "ns16550a");
    if (uart < 0)
        uart = fdt_node_offset_by_compatible(g_fdt, -1, "ns16550");
    if (uart < 0)
        return uart;

    err = decode_reg_entry_with_parent(uart, 0, base, size);
    if (err)
        return err;

    *node = uart;
    return 0;
}

/*
Odczytuje właściwość stdout-path z węzła /chosen i zwraca offset węzła wskazanego przez tę ścieżkę.
Obsługuje zarówno bezpośrednie ścieżki (zaczynające się od '/'), jak i aliasy z /aliases.
Ścieżka jest obcinana przy znaku ':' (separator opcji baud rate), żeby uzyskać czystą ścieżkę węzła.
Zwraca 0 i zapisuje offset do *node przy sukcesie, lub kod błędu libfdt gdy /chosen, stdout-path lub węzeł docelowy nie istnieje.
Używana przy inicjalizacji konsoli do automatycznego wykrycia urządzenia wyjściowego wskazanego przez bootloader.
*/
int dtb_chosen_stdout(int *node)
{
    int err;
    int chosen;
    int len;
    int i;
    char path[128];
    const char *stdout_path;

    err = dtb_require_init();
    if (err)
        return err;

    if (!node)
        return -FDT_ERR_BADVALUE;

    chosen = fdt_path_offset(g_fdt, "/chosen");
    if (chosen < 0)
        return chosen;

    stdout_path = fdt_getprop(g_fdt, chosen, "stdout-path", &len);
    if (!stdout_path)
        return len;

    for (i = 0; i < len - 1 && i < (int)sizeof(path) - 1; i++) {
        if (stdout_path[i] == ':' || stdout_path[i] == '\0')
            break;
        path[i] = stdout_path[i];
    }
    path[i] = '\0';

    if (path[0] == '/') {
        int off = fdt_path_offset(g_fdt, path);
        if (off < 0)
            return off;
        *node = off;
        return 0;
    }

    {
        int aliases = fdt_path_offset(g_fdt, "/aliases");
        const char *alias_path;
        int alias_len;
        int off;

        if (aliases < 0)
            return aliases;

        alias_path = fdt_getprop(g_fdt, aliases, path, &alias_len);
        if (!alias_path)
            return alias_len;

        off = fdt_path_offset(g_fdt, alias_path);
        if (off < 0)
            return off;

        *node = off;
        return 0;
    }
}

/*
Sprawdza, czy węzeł node jest magistralą simple-bus przez weryfikację obecności "simple-bus" w jego liście compatible.
Zapisuje 1 do *yes jeśli tak, 0 w przeciwnym razie; zwraca 0 przy sukcesie lub -FDT_ERR_BADVALUE gdy yes jest NULL.
Używana przy przechodzeniu drzewa urządzeń do identyfikacji węzłów magistrali wymagających translacji adresów przez ranges.
*/
int dtb_is_simple_bus(int node, int *yes)
{
    int err = dtb_require_init();
    if (err)
        return err;

    if (!yes)
        return -FDT_ERR_BADVALUE;

    *yes = compat_has(node, "simple-bus") ? 1 : 0;
    return 0;
}

/*
Odczytuje parametry magistrali z węzła node: #address-cells, #size-cells i obecność właściwości ranges.
Zapisuje wartości do wskaźników addr_cells, size_cells i has_ranges (0 lub 1).
Zwraca 0 przy sukcesie lub kod błędu libfdt, jeśli któryś z parametrów jest nieprawidłowy.
Używana przy dekodowaniu adresów urządzeń na magistralach z translacją adresów, żeby wiedzieć, jak interpretować wpisy reg i ranges.
*/
int dtb_bus_info(int node, int *addr_cells, int *size_cells, int *has_ranges)
{
    int err = dtb_require_init();
    if (err)
        return err;

    if (!addr_cells || !size_cells || !has_ranges)
        return -FDT_ERR_BADVALUE;

    *addr_cells = fdt_address_cells(g_fdt, node);
    if (*addr_cells < 0)
        return *addr_cells;

    *size_cells = fdt_size_cells(g_fdt, node);
    if (*size_cells < 0)
        return *size_cells;

    *has_ranges = prop_exists(node, "ranges") ? 1 : 0;
    return 0;
}

/*
Iteruje po bezpośrednich podwęzłach węzła magistrali bus_node.
Gdy *child_node < 0, zwraca pierwszy podwęzeł przez fdt_first_subnode; w przeciwnym razie zwraca następny przez fdt_next_subnode.
Aktualizuje *child_node i zwraca 0 przy sukcesie, lub kod błędu libfdt (np. -FDT_ERR_NOTFOUND) gdy nie ma więcej podwęzłów.
Używana do przeglądania urządzeń podłączonych do konkretnej magistrali (np. simple-bus) bez rekurencji w głąb drzewa.
*/
int dtb_bus_walk(int bus_node, int *child_node)
{
    int err = dtb_require_init();
    if (err)
        return err;

    if (!child_node)
        return -FDT_ERR_BADVALUE;

    if (*child_node < 0)
        *child_node = fdt_first_subnode(g_fdt, bus_node);
    else
        *child_node = fdt_next_subnode(g_fdt, *child_node);

    return (*child_node < 0) ? *child_node : 0;
}
