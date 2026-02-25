#ifndef SBI_TIMER_H
#define SBI_TIMER_H

#include "sbi.h"

// Set timer

static inline struct sbiret sbi_set_timer(uint64_t stime_value)
{
    return sbi_ecall(
        SBI_EXT_TIME,          // extension ID
        SBI_EXT_TIME_SET_TIMER,    // function ID
        (long)stime_value,     // arg0 (low XLEN bits)
#if __riscv_xlen == 32
        (long)(stime_value >> 32),  // arg1 for RV32
#else
        0,                     // unused for RV64
#endif
        0, 0, 0, 0);
}


#endif /* SBI_TIMER_H */