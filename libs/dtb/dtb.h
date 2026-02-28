#ifndef DTB_H
#define DTB_H

#include <stdint.h>


/*
DTB_MAX_* makra w dtb.h (lines 6-11) definiują stałe pojemności buforów 
(np. 8 wpisów reg, 16 irq, 32 regiony pamięci). Dzięki temu wszystkie 
struktury mają stały rozmiar i można je łatwo używać na stosie; jeśli 
DTB zawiera więcej wpisów, nadmiar jest ignorowany (funkcje zwracają 
-FDT_ERR_NOSPACE), co upraszcza obsługę bez alokacji dynamicznej.
*/
#define DTB_MAX_REGS 8
#define DTB_MAX_IRQS 16
#define DTB_MAX_REFS 16
#define DTB_MAX_CPUS 16
#define DTB_MAX_MEM_REGIONS 32
#define DTB_MAX_INTC 8


/*
dtb_addr_t to najmniejszy element: para base/size, która reprezentuje 
pojedynczy zakres adresowy z właściwości reg lub z list pamięci 
(/memory, /reserved-memory). Funkcje takie jak decode_reg_list, 
dtb_device_read czy dtb_memory_regions wypelniają tablice tych struktur 
(np. dtb.c).
*/
typedef struct {
    uint64_t base;
    uint64_t size;
} dtb_addr_t;

/*
dtb_irq_t (dtb.h (lines 19-25)) zawiera numer przerwania (irq), 
phandle nadrzędnego kontrolera (parent_phandle) oraz liczbę komórek 
(cells). dtb_device_read i helpery przerwań używają tego do ułożenia 
„rozłożonych” wpisów interrupts zgodnie z kontrolerem przerwań (dtb.c).
*/
typedef struct {
    uint32_t irq;
    uint32_t parent_phandle;
    uint32_t cells;
} dtb_irq_t;

/*
dtb_device_t (dtb.h (lines 27-44)) to kompletny opis węzła „urządzenia”: 
offset, nazwa, pierwszy ciąg compatible, listy regs, irqs, oraz referencje 
do clocks, resets, dmas, gpios. Każda tablica ma pojemność ustaloną makrami 
DTB_MAX_*. Funkcja dtb_device_read wypełnia tę strukturę, a dtb_device_first/next
z niej korzystają przy iteracji po wszystkich urządzeniach dtb.c.
*/
typedef struct {
    int node;
    const char *name;
    const char *compatible;
    dtb_addr_t regs[DTB_MAX_REGS];
    int reg_count;
    dtb_irq_t irqs[DTB_MAX_IRQS];
    int irq_count;
    uint32_t clocks[DTB_MAX_REFS];
    int clock_count;
    uint32_t resets[DTB_MAX_REFS];
    int reset_count;
    uint32_t dmas[DTB_MAX_REFS];
    int dma_count;
    uint32_t gpios[DTB_MAX_REFS];
    int gpio_count;
} dtb_device_t;
/*
dtb_cpu_t (dtb.h (lines 46-54)) zawiera informacje z węzłów /cpus: hartid
(z reg), stringi isa, mmu-type, riscv,isa, flagę svinval (obecność riscv,svinval)
oraz status. dtb_cpu_read i dtb_cpu_list używają tego do wykrywania dostępnych rdzeni (dtb.c).
*/
typedef struct {
    uint32_t hartid;
    const char *isa;
    const char *mmu_type;
    const char *riscv_isa;
    int svinval;
    const char *status;
} dtb_cpu_t;


/*
dtb_intc_t (dtb.h (lines 56-63)) opisuje kontroler przerwań: offset,
typ, phandle, liczbę komórek interrupt-cells i jego własne regiony MMIO (regs).
Jest wypełniana przez dtb_interrupt_controller_read/scan, dzięki czemu kod 
potrafi odczytać właściwości kontrolera przerwań, który obsługuje dane 
urządzenie (dtb.c (lines 725-780)).
*/
typedef struct {
    int node;
    const char *type;
    uint32_t phandle;
    uint32_t interrupt_cells;
    dtb_addr_t regs[DTB_MAX_REGS];
    int reg_count;
} dtb_intc_t;

int dtb_init(void *dtb);
const void *dtb_get(void);

int dtb_node_addr_cells(int node, int *cells);
int dtb_node_size_cells(int node, int *cells);
int dtb_decode_reg(int node, int index, uint64_t *base, uint64_t *size);
int dtb_translate_ranges(int node, uint64_t child_addr, uint64_t *cpu_addr);

int dtb_device_first(int *node);
int dtb_device_next(int *node);
int dtb_device_read(int node, dtb_device_t *out);
int dtb_find_compatible(const char *compat, int *node);
int dtb_find_compatible_n(const char *compat, int start_node, int *node);

int dtb_interrupt_controller_read(int node, dtb_intc_t *out);
int dtb_interrupt_controllers_scan(dtb_intc_t *arr, int cap, int *count);
int dtb_interrupt_map_device(int dev_node, dtb_irq_t *arr, int cap, int *count);
int dtb_detect_plic(int *node);
int dtb_detect_clint(int *node);
int dtb_detect_imsic(int *node);

int dtb_get_timebase(uint32_t *timebase);
int dtb_get_u32(int node, const char *prop, uint32_t *out);
int dtb_get_clock_frequency(int node, uint64_t *freq);
int dtb_get_device_clocks(int node, uint32_t *clks, int cap, int *count);
int dtb_get_timer_node(int *node);

int dtb_get_cpu_count(int *count);
int dtb_cpu_read(int cpu_node, dtb_cpu_t *out);
int dtb_cpu_list(dtb_cpu_t *arr, int cap, int *count);
int dtb_cpu_find_hart(uint32_t hartid, int *cpu_node);

int dtb_get_memory(uint64_t *base, uint64_t *size);
int dtb_memory_regions(dtb_addr_t *arr, int cap, int *count);
int dtb_reserved_memory_regions(dtb_addr_t *arr, int cap, int *count);
int dtb_memory_total(uint64_t *bytes);

int dtb_uart_ns16550a(int *node, uint64_t *base, uint64_t *size);
int dtb_chosen_stdout(int *node);

int dtb_is_simple_bus(int node, int *yes);
int dtb_bus_info(int node, int *addr_cells, int *size_cells, int *has_ranges);
int dtb_bus_walk(int bus_node, int *child_node);

#endif // DTB_H

/*TODO
    Popraw limity na sztywno zakodowane poprzez makra:
    #define DTB_MAX_REGS 8
    #define DTB_MAX_IRQS 16
    #define DTB_MAX_REFS 16
    #define DTB_MAX_CPUS 16
    #define DTB_MAX_MEM_REGIONS 32
    #define DTB_MAX_INTC 8
*/
