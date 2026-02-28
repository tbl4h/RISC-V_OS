.section .text
.global _start
_start:
    lla sp, stack_top
    call kmain
