// 64-bit multiply (what FxMul's int64 product compiles to) as ARM code in IWRAM, in place of libgcc's: Thumb has no
// long multiply, so libgcc builds it from 16-bit pieces in ROM, which the profiler had at ~6% of a busy frame.
//
//   int64_t __aeabi_lmul(int64_t a, int64_t b);   (a in r0:r1, b in r2:r3, the low 64 bits of a * b in r0:r1)

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

    .global __aeabi_lmul
    .type __aeabi_lmul, %function
    .global __muldi3
    .type __muldi3, %function
__aeabi_lmul:
__muldi3:
    mul     r3, r0, r3          // a.lo * b.hi
    mla     r1, r2, r1, r3      // + b.lo * a.hi
    umull   r0, r12, r2, r0     // a.lo * b.lo, 64-bit
    add     r1, r1, r12
    bx      lr
    .size __aeabi_lmul, . - __aeabi_lmul
    .size __muldi3, . - __muldi3
