.section .text
.global _start
_start:
    lla sp, _stack_top
    call kmain
