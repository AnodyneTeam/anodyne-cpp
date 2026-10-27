#pragma once

// Pure decode/timing logic, deliberately free of any GBA register access -
// this is what makes it possible to compile the exact same code (not a
// reimplementation) into a host-side test harness that can simulate VBlank/
// Timer/DMA timing and verify it, instead of relying on someone listening to
// a real device/emulator for every change. See tools/audio_sim/ for that
// harness; audio_manager.cpp is the GBA build, which owns everything that
// actually touches hardware (REG_*, irq_*) and calls into this.

#include "audio_manager.h" // AudioTrackData/AudioFrameRef/AUDIO_MAX_FRAME_SAMPLES

#include <stdint.h>

// Start of the compiled audio_blob.bin pool. The GBA build points this at
// the linked-in ROM blob (_binary_audio_blob_bin_start); a host-side
// simulator points it at a loaded copy of the same file - either way,
// DecodeCursor_* below reads through this pointer, never anything target-
// specific, so both run byte-identical decode logic against byte-identical
// data.
extern const uint8_t* g_AudioBlobBase;

// Streaming QOA decoder state. Decodes one 20-sample QOA slice at a time,
// on demand, rather than a whole frame up front - a whole-frame decode
// (qoa_decode_frame) takes ~1.9 VBlanks of CPU on the GBA, and doing it as a
// burst inside the VBlank ISR starved the sound DMA at every frame boundary
// (measured with mGBA: ~340 samples of zeros/stale data every 1280 samples -
// the "click"). Per-slice decoding spreads that cost evenly, ~176 samples'
// worth per VBlank. Output is bit-identical to qoa_decode_frame (checked by
// tools/audio_sim against it).
struct DecodeCursor final
{
    const AudioTrackData* track = nullptr;
    const uint8_t* slicePtr = nullptr; // next unread 8-byte slice of the current frame
    uint32_t sliceWords[2] = {};       // the slice's 3-bit residuals 0-9 and 10-19, each word MSB-first, as used
    uint32_t sliceUsed = 0;            // residuals of the current slice decoded so far
    const int32_t* dequantRow = nullptr; // dequant table row for the current slice's scalefactor
    uint32_t frameSamplesLeft = 0;     // samples in the current frame not yet loaded into a slice
    uint32_t sliceSamplesLeft = 0;     // samples left to decode in the current slice
    int32_t history[4] = {};
    int32_t weights[4] = {};
    int32_t lastSample = 0;  // previous decoded sample (for 2x upsampling's interpolation)
    uint32_t frameIndex = 0; // index of the NEXT frame to start
    uint8_t active = 0;
    uint8_t loop = 0;
};

void DecodeCursor_Start(DecodeCursor* c, const AudioTrackData* track, bool loop);

// On the GBA the decoder is the audio ISR's hot path, so it runs as 32-bit ARM
// code from IWRAM (1-cycle fetch) instead of Thumb from ROM - ~several times
// faster. IWRAM is ~0x05000000 away from ROM, beyond BL's range, hence
// long_call on the declaration. Host builds (tools/audio_sim) get neither.
#if defined(__arm__)
#define AUDIO_DECODE_IWRAM_DECL __attribute__((long_call))
#define AUDIO_DECODE_IWRAM_DEF __attribute__((section(".iwram"), target("arm"), long_call))
#define AUDIO_DECODE_IWRAM_DATA __attribute__((section(".iwram.data")))
#else
#define AUDIO_DECODE_IWRAM_DATA
#define AUDIO_DECODE_IWRAM_DECL
#define AUDIO_DECODE_IWRAM_DEF
#endif

// Decodes up to `count` samples and ADDS them, at full 16-bit scale, into
// accum[0..count) - every stream (BGM, ambience, SFX) mixes into one shared
// accumulator. Returns how many were decoded (less than `count` only if the
// cursor ran out and isn't looping).
AUDIO_DECODE_IWRAM_DECL uint32_t DecodeCursor_Mix16(DecodeCursor* c, int32_t* accum, uint32_t count);

// Same, for a stream at HALF the output rate: decodes outCount/2 samples and
// adds 2 outputs per sample (the first linearly interpolated from the
// previous sample, the second the sample itself) into accum[0..outCount).
// Returns output samples written. outCount must be even.
AUDIO_DECODE_IWRAM_DECL uint32_t DecodeCursor_Mix16Up2(DecodeCursor* c, int32_t* accum, uint32_t outCount);

#ifdef ANODYNE_PERF
// The C++ loop alone (no qoa_run.s), for the perf build's check of the hand-written one.
uint32_t DecodeCursor_Mix16Ref(DecodeCursor* c, int32_t* accum, uint32_t count);
uint32_t DecodeCursor_Mix16Up2Ref(DecodeCursor* c, int32_t* accum, uint32_t outCount);
#endif

// The GBA outputs DirectSound at 8 bits per FIFO, but its mixer computes
// 4*A + 2*B (FIFO A at 100% volume, FIFO B at 50%) and the default SOUNDBIAS
// PWM output keeps 9 bits. So both FIFOs together act as one 9-bit DAC:
// A = v >> 1, B = v & 1 gives 4*A + 2*B = 2*v exactly, i.e. +1 bit (~6dB
// less noise) over 8-bit - the one resolution gain the hardware allows.
// Converts the mixed accumulator to 9-bit (clamped) and splits it.
AUDIO_DECODE_IWRAM_DECL void AudioMix_SplitTo9Bit(const int32_t* accum, int8_t* outA, int8_t* outB, uint32_t count);

// A requested sample rate (e.g. 10512) and the timer reload value that
// actually produces it are NOT perfectly consistent: 16777216/sampleRate
// isn't an integer, so the real hardware rate ends up very slightly off from
// the nominal one. Deriving both the reload value AND the software refill rate
// (ComputeSamplesPerVBlankQ16FromDivisor) from the SAME divisor guarantees
// they can never drift apart - see audio_manager.cpp's original comment
// (git history) for the bug this fixes.
uint32_t ComputeTimerDivisor(uint32_t sampleRate);
uint16_t ComputeTimerReload(uint32_t divisor);
uint32_t ComputeSamplesPerVBlankQ16FromDivisor(uint32_t divisor);
