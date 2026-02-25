#ifndef SBI_HART_STATE_MANAGEMENT_EXTENSION_H
#define SBI_HART_STATE_MANAGEMENT_EXTENSION_H
#include "sbi.h"

//  HART start
static inline struct sbiret sbi_hart_start(unsigned long hartid,
                                           unsigned long start_addr,
                                           unsigned long opaque)
{
    return sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_START,
                     hartid,
                     start_addr, opaque,
                     0, 0, 0);
}

//  HART stop
static inline struct sbiret sbi_hart_stop(void)
{
    return sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_STOP,
                     0, 0, 0, 0, 0, 0);
}

//  HART get status
static inline struct sbiret sbi_hart_get_status(unsigned long hartid)
{
    return sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_GET_STATUS,
                     hartid, 0, 0, 0, 0, 0);
}

//  HART get status
static inline struct sbiret sbi_hart_suspend(unsigned long suspend_type,
                                             unsigned long resume_addr,
                                             unsigned long opaque)
{
    return sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_SUSPEND,
                     suspend_type, resume_addr, opaque, 0, 0, 0);
}



#endif /* SBI_HART_STATE_MANAGEMENT_EXTENSION_H */