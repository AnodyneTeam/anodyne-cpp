#include "audio_decode_core.hpp"

const uint8_t* g_AudioBlobBase = nullptr;

#if defined(__arm__)
// platform/gba/sys/qoa_run.s: the inner loop below by hand (history[4] then weights[4] at lms).
extern "C" AUDIO_DECODE_IWRAM_DECL uint32_t QoaRun(int32_t* out, int32_t* end, const int32_t* row, uint32_t bits,
                                                   int32_t* lms);
extern "C" AUDIO_DECODE_IWRAM_DECL uint32_t QoaRunUp2(int32_t* out, int32_t* end, const int32_t* row, uint32_t bits,
                                                      int32_t* lms);
#endif

namespace
{
    // Copy of qoa.h's qoa_dequant_tab (private to its QOA_IMPLEMENTATION
    // block). In IWRAM on the GBA, where the decoder reads it every sample.
    AUDIO_DECODE_IWRAM_DATA const int32_t kQoaDequant[16][8] = {
        {1, -1, 3, -3, 5, -5, 7, -7},
        {5, -5, 18, -18, 32, -32, 49, -49},
        {16, -16, 53, -53, 95, -95, 147, -147},
        {34, -34, 113, -113, 203, -203, 315, -315},
        {63, -63, 210, -210, 378, -378, 588, -588},
        {104, -104, 345, -345, 621, -621, 966, -966},
        {158, -158, 528, -528, 950, -950, 1477, -1477},
        {228, -228, 760, -760, 1368, -1368, 2128, -2128},
        {316, -316, 1053, -1053, 1895, -1895, 2947, -2947},
        {422, -422, 1405, -1405, 2529, -2529, 3934, -3934},
        {548, -548, 1828, -1828, 3290, -3290, 5117, -5117},
        {696, -696, 2320, -2320, 4176, -4176, 6496, -6496},
        {868, -868, 2893, -2893, 5207, -5207, 8099, -8099},
        {1064, -1064, 3548, -3548, 6386, -6386, 9933, -9933},
        {1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005},
        {1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336},
    };

    const uint32_t kQoaSliceLen = 20;
    const uint32_t kQoaLmsLen = 4;

    inline __attribute__((always_inline)) uint32_t ReadU32BE(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    }

    inline __attribute__((always_inline)) int16_t ReadS16BE(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }

    // Loads the next frame's header + LMS state (mono only - the compiler only
    // ever emits mono). Returns false if the track ended and isn't looping.
    // always_inline: these must be folded into DecodeCursor_Mix16 so they
    // live in IWRAM with it rather than being a ROM<->IWRAM far call.
    inline __attribute__((always_inline)) bool BeginNextFrame(DecodeCursor* c)
    {
        if (c->frameIndex >= c->track->frameCount)
        {
            if (!c->loop)
            {
                c->active = 0;
                return false;
            }
            c->frameIndex = c->track->loopStartFrame;
        }

        const AudioFrameRef* frameRef = &c->track->frames[c->frameIndex++];
        const uint8_t* p = g_AudioBlobBase + frameRef->offset;

        // Frame header: channels(8) samplerate(24) samples(16) frame_size(16).
        c->frameSamplesLeft = (static_cast<uint32_t>(p[4]) << 8) | p[5];
        p += 8;
        for (uint32_t i = 0; i < kQoaLmsLen; i++)
        {
            c->history[i] = ReadS16BE(p + i * 2);
            c->weights[i] = ReadS16BE(p + 8 + i * 2);
        }
        c->slicePtr = p + 16;
        c->sliceSamplesLeft = 0;
        return true;
    }

    // Loads the next 8-byte slice: 4-bit scalefactor + 20 3-bit residuals. The residuals go to two words (0-9 and
    // 10-19, each from the top), so the hot loop shifts one 32-bit word rather than a 64-bit one.
    inline __attribute__((always_inline)) void BeginNextSlice(DecodeCursor* c)
    {
        uint64_t slice = (static_cast<uint64_t>(ReadU32BE(c->slicePtr)) << 32) | ReadU32BE(c->slicePtr + 4);
        c->slicePtr += 8;
        c->dequantRow = kQoaDequant[slice >> 60];
        c->sliceWords[0] = static_cast<uint32_t>(slice >> 30) << 2;
        c->sliceWords[1] = static_cast<uint32_t>(slice) << 2;
        c->sliceUsed = 0;
        c->sliceSamplesLeft = (c->frameSamplesLeft < kQoaSliceLen) ? c->frameSamplesLeft : kQoaSliceLen;
        c->frameSamplesLeft -= c->sliceSamplesLeft;
    }

    // Shared body of DecodeCursor_Mix16/_Mix16Up2: decodes `count` samples
    // and adds them into accum - one output per sample, or (kUp2) two, the
    // first linearly interpolated from the previous sample. always_inline so
    // each IWRAM entry point gets its own branch-free copy of the hot loop.
    // kAsm: the GBA's hand-written loop (qoa_run.s) where it can (the 2x one needs the last sample to be the
    // history's newest, which only a new frame's header breaks).
    template <bool kUp2, bool kAsm>
    inline __attribute__((always_inline)) uint32_t MixImpl(DecodeCursor* c, int32_t* accum, uint32_t count)
    {
        uint32_t written = 0;
        while (written < count && c->active)
        {
            if (c->sliceSamplesLeft == 0)
            {
                if (c->frameSamplesLeft == 0 && !BeginNextFrame(c))
                {
                    break;
                }
                if (c->frameSamplesLeft == 0)
                {
                    continue; // empty frame - defensive, the compiler never emits one
                }
                BeginNextSlice(c);
            }

            // Inner loop on locals (spilling the LMS state back to the cursor
            // only once per run) - this is the ISR's hot path.
            // (a run stays within one of the slice's two residual words)
            const uint32_t half = c->sliceUsed >= 10 ? 1 : 0;
            uint32_t n = count - written;
            if (n > c->sliceSamplesLeft)
            {
                n = c->sliceSamplesLeft;
            }
            if (n > 10 * (half + 1) - c->sliceUsed)
            {
                n = 10 * (half + 1) - c->sliceUsed;
            }
            c->sliceSamplesLeft -= n;
            c->sliceUsed += n;
#if defined(__arm__)
            if (kAsm && (!kUp2 || c->lastSample == c->history[3]))
            {
                int32_t* const runOut = accum + (kUp2 ? written * 2 : written);
                int32_t* const runEnd = runOut + (kUp2 ? n * 2 : n);
                c->sliceWords[half] = (kUp2 ? QoaRunUp2 : QoaRun)(runOut, runEnd, c->dequantRow, c->sliceWords[half],
                                                                  c->history);
                c->lastSample = c->history[3];
                written += n;
                continue;
            }
#endif

            int32_t h0 = c->history[0], h1 = c->history[1], h2 = c->history[2], h3 = c->history[3];
            int32_t w0 = c->weights[0], w1 = c->weights[1], w2 = c->weights[2], w3 = c->weights[3];
            uint32_t bits = c->sliceWords[half];
            const int32_t* row = c->dequantRow;
            int32_t* out = accum + (kUp2 ? written * 2 : written);
            int32_t* const end = out + (kUp2 ? n * 2 : n);
            int32_t last = c->lastSample;

            // Same math as qoa.h's qoa_decode_frame/qoa_lms_predict/qoa_lms_update.
            while (out != end)
            {
                int32_t predicted = (w0 * h0 + w1 * h1 + w2 * h2 + w3 * h3) >> 13;
                int32_t dequantized = row[bits >> 29];
                bits <<= 3;
                int32_t reconstructed = predicted + dequantized;
                if (reconstructed > 32767)
                {
                    reconstructed = 32767;
                }
                else if (reconstructed < -32768)
                {
                    reconstructed = -32768;
                }

                const int32_t delta = dequantized >> 4;
                w0 += (h0 < 0) ? -delta : delta;
                w1 += (h1 < 0) ? -delta : delta;
                w2 += (h2 < 0) ? -delta : delta;
                w3 += (h3 < 0) ? -delta : delta;
                h0 = h1;
                h1 = h2;
                h2 = h3;
                h3 = reconstructed;

                if (kUp2)
                {
                    out[0] += (last + reconstructed) >> 1;
                    out[1] += reconstructed;
                    out += 2;
                }
                else
                {
                    *out++ += reconstructed;
                }
                last = reconstructed;
            }

            c->lastSample = last;

            c->history[0] = h0;
            c->history[1] = h1;
            c->history[2] = h2;
            c->history[3] = h3;
            c->weights[0] = w0;
            c->weights[1] = w1;
            c->weights[2] = w2;
            c->weights[3] = w3;
            c->sliceWords[half] = bits;
            written += n;
        }
        return written;
    }
}

void DecodeCursor_Start(DecodeCursor* c, const AudioTrackData* track, bool loop)
{
    *c = DecodeCursor{};
    c->track = track;
    c->loop = loop ? 1 : 0;
    c->active = (track != nullptr && track->frameCount > 0) ? 1 : 0;
}


AUDIO_DECODE_IWRAM_DEF uint32_t DecodeCursor_Mix16(DecodeCursor* c, int32_t* accum, uint32_t count)
{
    return MixImpl<false, true>(c, accum, count);
}

AUDIO_DECODE_IWRAM_DEF uint32_t DecodeCursor_Mix16Up2(DecodeCursor* c, int32_t* accum, uint32_t outCount)
{
    return MixImpl<true, true>(c, accum, outCount / 2) * 2;
}

#ifdef ANODYNE_PERF
uint32_t DecodeCursor_Mix16Ref(DecodeCursor* c, int32_t* accum, uint32_t count)
{
    return MixImpl<false, false>(c, accum, count);
}

uint32_t DecodeCursor_Mix16Up2Ref(DecodeCursor* c, int32_t* accum, uint32_t outCount)
{
    return MixImpl<true, false>(c, accum, outCount / 2) * 2;
}
#endif

namespace
{
    inline __attribute__((always_inline)) int32_t To9Bit(int32_t accum)
    {
        int32_t v = accum >> 7; // 16-bit scale -> 9-bit
        if (v > 255)
        {
            v = 255;
        }
        else if (v < -256)
        {
            v = -256;
        }
        return v;
    }
}

AUDIO_DECODE_IWRAM_DEF void AudioMix_SplitTo9Bit(const int32_t* accum, int8_t* outA, int8_t* outB, uint32_t count)
{
    uint32_t i = 0;
    // Four samples to a word where the buffers allow: a byte store to EWRAM costs as much as a word one.
    if (((reinterpret_cast<uintptr_t>(outA) | reinterpret_cast<uintptr_t>(outB)) & 3) == 0)
    {
        for (; i + 4 <= count; i += 4)
        {
            uint32_t a = 0;
            uint32_t b = 0;
            for (uint32_t k = 0; k < 4; k++)
            {
                const int32_t v = To9Bit(accum[i + k]);
                a |= static_cast<uint32_t>(static_cast<uint8_t>(v >> 1)) << (8 * k);
                b |= static_cast<uint32_t>(v & 1) << (8 * k);
            }
            *reinterpret_cast<uint32_t*>(outA + i) = a; // (little-endian: sample i in the low byte)
            *reinterpret_cast<uint32_t*>(outB + i) = b;
        }
    }
    for (; i < count; i++)
    {
        const int32_t v = To9Bit(accum[i]);
        outA[i] = static_cast<int8_t>(v >> 1);
        outB[i] = static_cast<int8_t>(v & 1);
    }
}

uint32_t ComputeTimerDivisor(uint32_t sampleRate)
{
    // Rounded, not truncated: 16777216/13379 = 1253.98, and only 1254 gives
    // a whole 224 samples per VBlank (see ValidateGbaSampleRate in the
    // content compiler, which must mirror this).
    uint32_t divisor = (16777216u + sampleRate / 2) / sampleRate;
    return (divisor < 1) ? 1 : divisor;
}

uint16_t ComputeTimerReload(uint32_t divisor) { return (uint16_t)(65536u - divisor); }

uint32_t ComputeSamplesPerVBlankQ16FromDivisor(uint32_t divisor)
{
    // samples/VBlank = actualRate * (280896 cycles/VBlank) / (16777216 cycles/sec)
    //                = (16777216/divisor) * 280896 / 16777216
    //                = 280896 / divisor
    // ...in Q16.16:
    uint64_t numerator = (uint64_t)280896ull << 16;
    return (uint32_t)(numerator / divisor);
}
