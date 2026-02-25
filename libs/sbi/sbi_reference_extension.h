#ifndef SBI_REFERENCE_EXTENSION_H
#define SBI_REFERENCE_EXTENSION_H

#include "sbi.h"

// Remote FENCE.I
static inline struct sbiret sbi_remote_fence_i(
    unsigned long hart_mask,
    unsigned long hart_mask_base
){
    return sbi_ecall(SBI_EXT_RFENCE, 
                    SBI_EXT_RFENCE_REMOTE_FENCE_I,
                     hart_mask, hart_mask_base,
                      0, 0, 0, 0);
}

// Remote SFENCE.VMA 
static inline struct sbiret sbi_remote_sfence_vma(
    unsigned long hart_mask,
    unsigned long hart_mask_base,
    unsigned long start_addr,
    unsigned long size
){
    return sbi_ecall(SBI_EXT_RFENCE,
                    SBI_EXT_RFENCE_REMOTE_SFENCE_VMA,
                     hart_mask, hart_mask_base,
                      start_addr, size, 0, 0);
}

// Remote SFENCE.VMA with ASID
static inline struct sbiret sbi_remote_sfence_vma_asid(
    unsigned long hart_mask,
    unsigned long hart_mask_base,
    unsigned long start_addr,
    unsigned long size,
    unsigned long asid
){
    return sbi_ecall(SBI_EXT_RFENCE,
                    SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID,
                     hart_mask, hart_mask_base,
                      start_addr, size, asid, 0);
}

// Remote HFENCE.GVMA with VMID
static inline struct sbiret sbi_remote_hfence_gvma_vmid(
    unsigned long hart_mask,
    unsigned long hart_mask_base,
    unsigned long start_addr,
    unsigned long size,
    unsigned long vmid
){
    return sbi_ecall(SBI_EXT_RFENCE,
                    SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID,
                     hart_mask, hart_mask_base,
                      start_addr, size, vmid, 0);
}
// Remote HFENCE.GVMA
static inline struct sbiret sbi_remote_hfence_gvma(
    unsigned long hart_mask,
    unsigned long hart_mask_base,
    unsigned long start_addr,
    unsigned long size
){
    return sbi_ecall(SBI_EXT_RFENCE,
                    SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA,
                     hart_mask, hart_mask_base,
                      start_addr, size, 0, 0);
}

// Remote HFENCE.VVMA with ASID
static inline struct sbiret sbi_remote_hfence_vvma_asid(unsigned long hart_mask,
unsigned long hart_mask_base,
unsigned long start_addr,
unsigned long size,
unsigned long asid){
    return sbi_ecall(SBI_EXT_RFENCE,
                    SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID,
                     hart_mask, hart_mask_base,
                      start_addr, size, asid, 0);
}

//  Remote HFENCE.VVMA 
static inline struct sbiret sbi_remote_hfence_vvma(
    unsigned long hart_mask,
    unsigned long hart_mask_base,
    unsigned long start_addr,
    unsigned long size
){
    return sbi_ecall(SBI_EXT_RFENCE,
                    SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA,
                     hart_mask, hart_mask_base,
                      start_addr, size, 0, 0);
}

#endif /* SBI_REFERENCE_EXTENSION_H */