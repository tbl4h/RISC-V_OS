#ifndef SBI_BASE_H
#define SBI_BASE_H

#include "sbi.h"
#include <sbi/sbi_ecall_interface.h>


// Get SBI specification version
static inline struct sbiret sbi_get_sbi_spec_version(void){
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_GET_SPEC_VERSION,
                     0,0,0,0,0,0);
}

// Get SBI implementation ID 
static inline struct sbiret sbi_get_impl_id(void){
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_GET_IMP_ID,
                     0,0,0,0,0,0);
}

// Get SBI implementation version
static inline struct sbiret sbi_get_impl_version(void){
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_GET_IMP_VERSION,
                     0,0,0,0,0,0);
}

// Probe SBI extension
static inline struct sbiret sbi_probe_extension(long extension_id)
{
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_PROBE_EXT,
                     extension_id,0,0,0,0,0);
};

// Get machine vendor ID
static inline struct sbiret sbi_get_mvendorid(){
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_GET_MVENDORID
                     ,0,0,0,0,0,0);
}

// Get machine architecture ID
static inline struct sbiret sbi_get_marchid() {
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_GET_MARCHID
                     ,0,0,0,0,0,0);
}

// Get machine implementation ID
static inline struct sbiret sbi_get_mimpid() {
    return sbi_ecall(SBI_EXT_BASE,
                     SBI_EXT_BASE_GET_MARCHID
                     ,0,0,0,0,0,0);
}

#endif