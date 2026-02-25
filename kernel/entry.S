.section .text
.global _start
_start:
    la sp, stack_top
    call kmain