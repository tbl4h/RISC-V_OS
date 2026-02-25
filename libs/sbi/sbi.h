#ifndef SBI_H
#define SBI_H

#include <stdint.h>
#include <sbi/sbi_ecall_interface.h>


/*
 * sbiret - Structure representing the return value of an SBI call.
 */
struct sbiret {
    long error;
    long value;
};

/*
 * sbi_ecall - Perform an SBI trap with the provided extension/function
 * numbers and up to six arguments.
 */
static inline struct sbiret sbi_ecall(long ext, long fid,
                             long arg0, long arg1,
                             long arg2, long arg3,
                             long arg4, long arg5)
{
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a3 asm("a3") = arg3;
    register long a4 asm("a4") = arg4;
    register long a5 asm("a5") = arg5;
    register long a6 asm("a6") = fid;
    register long a7 asm("a7") = ext;

    /*
     * The SBI standard routes calls through ecall, clobbering memory and the
     * a0/a1 registers so we mark them as read/write in the constraint list.
     */
    asm volatile ("ecall"
                  : "+r"(a0), "+r"(a1)
                  : "r"(a2), "r"(a3), "r"(a4),
                    "r"(a5), "r"(a6), "r"(a7)
                  : "memory");

    struct sbiret ret;
    ret.error = a0;   // a0 = error
    ret.value = a1;   // a1 = return value

    return ret;   // Return the structure containing both error and value
}


#endif
