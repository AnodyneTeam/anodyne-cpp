// The QOA decoder's inner loop (audio_decode_core.cpp's MixImpl) by hand, as ARM code in IWRAM: the VBlank interrupt
// runs it for every sample of every playing sound, and the compiler's version spills registers to the stack.
// Same maths, sample for sample (the perf build checks it against the C++ loop at boot).
//
//   uint32_t QoaRun(int32_t* out, int32_t* end, const int32_t* row, uint32_t bits, int32_t* lms);
//   uint32_t QoaRunUp2(int32_t* out, int32_t* end, const int32_t* row, uint32_t bits, int32_t* lms);
//
// Decodes samples into out (adding to what's there) until it reaches end: QoaRun one output a sample, QoaRunUp2
// two (the first halfway from the previous sample, which must be lms[3]). row is the slice's dequant row, bits its
// residuals MSB-first; lms is history[4] then weights[4], read and written back. Returns the bits left.

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

// One sample: r4-r7 history, r8-r11 weights, r2 row, r3 bits; leaves the clamped sample in r12, lr free.
.macro QOA_SAMPLE
    mul     r12, r8, r4
    mla     r12, r9, r5, r12
    mla     r12, r10, r6, r12
    mla     r12, r11, r7, r12
    mov     lr, r3, lsr #29
    ldr     lr, [r2, lr, lsl #2]        // dequantized
    mov     r3, r3, lsl #3
    add     r12, lr, r12, asr #13       // reconstructed = predicted + dequantized
    cmp     r12, #0x8000                // clamped to -32768..32767
    movge   r12, #0x7F00
    orrge   r12, r12, #0xFF
    cmn     r12, #0x8000
    mvnlt   r12, #0x7F00
    biclt   r12, r12, #0xFF
    mov     lr, lr, asr #4              // delta: w += h < 0 ? -delta : delta
    cmp     r4, #0
    addge   r8, r8, lr
    sublt   r8, r8, lr
    cmp     r5, #0
    addge   r9, r9, lr
    sublt   r9, r9, lr
    cmp     r6, #0
    addge   r10, r10, lr
    sublt   r10, r10, lr
    cmp     r7, #0
    addge   r11, r11, lr
    sublt   r11, r11, lr
.endm

.macro QOA_SHIFT
    mov     r4, r5
    mov     r5, r6
    mov     r6, r7
    mov     r7, r12
.endm

    .global QoaRun
    .type   QoaRun, %function
QoaRun:
    push    {r4-r11, lr}
    ldr     r12, [sp, #36]              // lms
    push    {r12}
    ldmia   r12, {r4-r11}
    cmp     r0, r1
    beq     2f
1:
    QOA_SAMPLE
    ldr     lr, [r0]
    add     lr, lr, r12
    str     lr, [r0], #4
    QOA_SHIFT
    cmp     r0, r1
    bne     1b
2:
    pop     {r12}
    stmia   r12, {r4-r11}
    mov     r0, r3
    pop     {r4-r11, lr}
    bx      lr
    .size   QoaRun, . - QoaRun

    .global QoaRunUp2
    .type   QoaRunUp2, %function
QoaRunUp2:
    push    {r4-r11, lr}
    ldr     r12, [sp, #36]              // lms
    push    {r12}
    ldmia   r12, {r4-r11}
    cmp     r0, r1
    beq     2f
1:
    QOA_SAMPLE
    add     r4, r7, r12                 // (h0 is done with) previous + this
    ldr     lr, [r0]
    add     lr, lr, r4, asr #1
    str     lr, [r0], #4
    ldr     lr, [r0]
    add     lr, lr, r12
    str     lr, [r0], #4
    QOA_SHIFT
    cmp     r0, r1
    bne     1b
2:
    pop     {r12}
    stmia   r12, {r4-r11}
    mov     r0, r3
    pop     {r4-r11, lr}
    bx      lr
    .size   QoaRunUp2, . - QoaRunUp2
