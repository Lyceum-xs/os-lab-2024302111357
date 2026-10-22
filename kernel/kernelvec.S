.section .text
.globl kernelvec
.balign 16
kernelvec:
        addi sp, sp, -160
        sd ra, 0(sp)
        sd gp, 8(sp)
        sd tp, 16(sp)
        sd t0, 24(sp)
        sd t1, 32(sp)
        sd t2, 40(sp)
        sd a0, 48(sp)
        sd a1, 56(sp)
        sd a2, 64(sp)
        sd a3, 72(sp)
        sd a4, 80(sp)
        sd a5, 88(sp)
        sd a6, 96(sp)
        sd a7, 104(sp)
        sd t3, 112(sp)
        sd t4, 120(sp)
        sd t5, 128(sp)
        sd t6, 136(sp)
        call kerneltrap
        ld ra, 0(sp)
        ld gp, 8(sp)
        ld tp, 16(sp)
        ld t0, 24(sp)
        ld t1, 32(sp)
        ld t2, 40(sp)
        ld a0, 48(sp)
        ld a1, 56(sp)
        ld a2, 64(sp)
        ld a3, 72(sp)
        ld a4, 80(sp)
        ld a5, 88(sp)
        ld a6, 96(sp)
        ld a7, 104(sp)
        ld t3, 112(sp)
        ld t4, 120(sp)
        ld t5, 128(sp)
        ld t6, 136(sp)
        addi sp, sp, 160
        sret
