#ifndef SBI_SYSTEM_RESET_EXTENSION_H
#define SBI_SYSTEM_RESET_EXTENSION_H
#include "sbi.h"

//  System reset
static inline struct sbiret sbi_system_reset
(uint32_t reset_type, uint32_t reset_reason)
{
    return sbi_ecall(SBI_EXT_SRST, SBI_EXT_SRST_RESET,
                     reset_type, reset_reason, 0, 0, 0, 0);    
}

#endif