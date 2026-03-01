#ifndef KERNEL_MEMORY_MAP_H
#define KERNEL_MEMORY_MAP_H

#include <stdint.h>
#include <platform_init.h>

/*
 * Flagi PTE (Page Table Entry) dla RISC-V Sv39
 * Zgodne ze specyfikacją RISC-V Privileged Architecture
 */
#define PTE_V     0x01  /* Valid - wpis jest ważny */
#define PTE_R     0x02  /* Readable - strona jest czytelna */
#define PTE_W     0x04  /* Writable - strona jest zapisywalna */
#define PTE_X     0x08  /* Executable - strona jest wykonywalna */
#define PTE_U     0x10  /* User accessible - dostępne z trybu użytkownika */
#define PTE_G     0x20  /* Global - globalny wpis */

/*
 * Złożone uprawnienia dla regionów pamięci (dla PTE)
 */
#define MM_R     PTE_R
#define MM_W     PTE_W
#define MM_X     PTE_X
#define MM_RW    (PTE_R | PTE_W)
#define MM_RX    (PTE_R | PTE_X)
#define MM_RWX   (PTE_R | PTE_W | PTE_X)
#define MM_NONE  0

/*
 * Flagi ochronne dla regionów pamięci
 * Używane przez alokator pamięci do określenia czy region może być alokowany
 */
#define MM_FLAG_ALLOCATABLE   0x01  /* Region może być alokowany */
#define MM_FLAG_RESERVED      0x02  /* Region zarezerwowany - NIE można alokować */
#define MM_FLAG_KERNEL        0x04  /* Region kernela - krytyczny */
#define MM_FLAG_BOOT          0x08  /* Region boot - krytyczny */
#define MM_FLAG_MMIO          0x10  /* Region urządzeń MMIO - nie można alokować */
#define MM_FLAG_DTB           0x20  /* Region DTB - krytyczny */

/*
 * Flagi stanu danych dla regionów pamięci
 */
#define MM_FLAG_INITIALIZED   0x40  /* Region ma zainicjalizowane dane */
#define MM_FLAG_UNINITIALIZED 0x80 /* Region niezainicjalizowany (np. .bss) */
#define MM_FLAG_DIRTY         0x100 /* Dane zostały zmodyfikowane */
#define MM_FLAG_CLEAN         0x200 /* Dane nie zostały zmodyfikowane */

/**
 * Struktura pojedynczego regionu pamięci z uprawnieniami i flagami ochronnymi.
 */
typedef struct {
    uint64_t start;           /* Adres początkowy */
    uint64_t end;             /* Adres końcowy */
    uint8_t pte_flags;        /* Flagi PTE (R/W/X) */
    uint8_t protect_flags;    /* Flagi ochronne (MM_FLAG_*) */
    const char *source;       /* Źródło regionu */
} mm_region_t;

/**
 * Struktura przechowująca stan mapy pamięci.
 * Używana do przekazywania danych między mm_stage2_build() a mm_stage2_dump().
 */
typedef struct {
    /* Wskaźniki do tablic regionów (zarządzane wewnętrznie) */
    mm_region_t *ram;
    int ram_count;
    mm_region_t *reserved;
    int reserved_count;
    mm_region_t *free;
    int free_count;
    
    /* Statystyki */
    uint64_t first_free_frame;
    uint32_t ram_pages;
    uint32_t reserved_pages;
    uint32_t reserved_pages_in_ram;
    uint32_t free_pages;
    
    /* Wyniki walidacji */
    int totals_ok;
    int first_free_ok;
    int overlap_free_reserved;
} mm_state_t;

int mm_stage2_build(const hw_state_t *hw);
int mm_stage2_dump(void);
int mm_stage2_build_and_dump(const hw_state_t *hw);

#endif
