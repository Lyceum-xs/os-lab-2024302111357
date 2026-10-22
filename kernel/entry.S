        # qemu -kernel loads the kernel at 0x80000000.
        # Hart 0 uses the boot stack; secondary harts stay parked.

#include "course_sid.h"
        .section .text
        .globl _entry
_entry:
        csrw    mie, zero
        csrr    a1, mhartid
        bnez    a1, park
        la      sp, stack0
        li      a0, 1024 * LAB1_STACK_KB
        addi    a1, a1, 1
        mul     a0, a0, a1
        add     sp, sp, a0

        call    start

park:
        wfi
        j       park
