// FxDiv's (a * 65536) / b (ecs.h) as ARM code in IWRAM, in place of libgcc's 64-bit divide (__aeabi_ldivmod): Thumb
// has no divide, and libgcc's general one in ROM had ~7% of a frame on screens full of bullets. A 48-by-32-bit long
// division, which is all FxDiv needs while the quotient fits in 32 bits; the rest go to FxDivSlow (libgcc's).
//
//   int32_t FxDivRaw(int32_t a, int32_t b);   (b != 0; the low 32 bits of the quotient, rounded towards zero)

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

    .global FxDivRaw
    .type FxDivRaw, %function
FxDivRaw:
    eor     r12, r0, r1         // the sign of the quotient, in bit 31
    movs    r2, r0
    rsbmi   r2, r2, #0          // |a| (unsigned: INT32_MIN's is 2^31)
    movs    r3, r1
    rsbmi   r3, r3, #0          // |b|
    cmp     r3, r2, lsr #16     // the quotient needs over 32 bits: libgcc's
    bls     1f
    stmfd   sp!, {r4}
    mov     r0, r2, lsr #16     // the remainder so far (always under |b|)
    mov     r1, r2, lsl #16     // the dividend's bits still to go, then the quotient's as they come in
    mov     r4, #4
2:
    .rept 8
    adds    r1, r1, r1
    adc     r0, r0, r0          // (2 * (|b| - 1) + 1 still fits in 32 bits)
    cmp     r0, r3
    subcs   r0, r0, r3
    orrcs   r1, r1, #1
    .endr
    subs    r4, r4, #1
    bne     2b
    ldmfd   sp!, {r4}
    cmp     r12, #0
    rsblt   r0, r1, #0
    movge   r0, r1
    bx      lr
1:
    ldr     r12, =FxDivSlow     // (r0, r1 are still a, b)
    bx      r12
    .size FxDivRaw, . - FxDivRaw
    .ltorg
