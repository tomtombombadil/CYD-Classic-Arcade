    .section .literal._start, "ax"
    .align 4
.Lstack:
    .word __stack_top
.Lmain:
    .word cmain
    .section .text._start, "ax"
    .global _start
    .align 4
_start:
    l32r a1, .Lstack
    l32r a8, .Lmain
    callx8 a8
1:  j 1b
