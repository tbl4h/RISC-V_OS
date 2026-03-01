#ifndef KERNEL_PLATFORM_INIT_H
#define KERNEL_PLATFORM_INIT_H

#include <stdint.h>

typedef struct {
    uint32_t boot_hartid;
    int boot_cpu_node;
    int cpu_count;
    uint64_t mem_base;
    uint64_t mem_size;
    uint64_t mem_total;
    uint32_t timebase_hz;
    int timer_node;
    int plic_node;
    int clint_node;
    int imsic_node;
} hw_state_t;

void init_sbi(const hw_state_t *hw);
void init_dtb(hw_state_t *hw, void *dtb, uint64_t hartid);
void init_timer(void);
void validate_and_dump_dtb_state(const hw_state_t *hw);

#endif
