#ifndef DTB_H
#define DTB_H

#include <stdint.h>

int dtb_init(void *dtb);                 // Waliduje i zapisuje wskaźnik
const void *dtb_get(void);               // Zwraca wskaźnik fdt
int dtb_get_memory(uint64_t *base, uint64_t *size);
int dtb_get_timebase(uint32_t *timebase);
int dtb_get_cpu_count(int *count);

#endif // DTB_H
