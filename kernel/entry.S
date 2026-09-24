        # qemu -kernel loads the kernel at 0x80000000.
        # Give every hart a private stack before entering C.

        .section .text
        .globl _entry
_entry:
        csrw    mie, zero
        la      sp, stack0
        li      a0, 4096 * 8
        csrr    a1, mhartid
        addi    a1, a1, 1
        mul     a0, a0, a1
        add     sp, sp, a0

        csrr    a1, mhartid
        bnez    a1, park
        call    start

park:
        wfi
        j       park
