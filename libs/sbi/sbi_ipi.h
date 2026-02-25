#ifndef SBI_IPI_H
#define SBI_IPI_H

#include "sbi.h"

// Send IPI to the specified harts using the SBI IPI extension
static inline struct sbiret sbi_send_ipi(unsigned long hart_mask,
unsigned long hart_mask_base){
    return sbi_ecall(SBI_EXT_IPI,
            SBI_EXT_IPI_SEND_IPI,
            hart_mask,
            hart_mask_base,
            0, 0, 0, 0);
}


#endif /* SBI_IPI_H */