// Direct Sound: FIFO A/B (32 bytes each, refilled by sound DMA when half empty), clocked by timer 0/1 overflows as
// SOUNDCNT_H selects, mixed at the hardware's 32768 Hz output rate. The PSG channels are not simulated (the game
// doesn't use them).

#include "sim_internal.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

namespace sim
{
namespace
{
constexpr int kOutRate = 32768;
constexpr int kCyclesPerOut = kClockHz / kOutRate; // 512

struct Fifo final
{
    int8_t mData[32];
    int mRead = 0;
    int mCount = 0;
    int8_t mSample = 0; // what the DAC is outputting (held until the next timer tick)
};
Fifo gFifo[2];

SDL_AudioStream* gStream = nullptr;
FILE* gWav = nullptr;
uint32_t gWavBytes = 0;
int gOutCycles = 0;
int16_t gFrameBuf[2 * 1024];
int gFrameSamples = 0;
uint64_t gUnderruns = 0;

void WavHeader()
{
    const uint32_t dataBytes = gWavBytes;
    const uint32_t riff = 36 + dataBytes;
    const uint16_t fmt = 1;
    const uint16_t ch = 2;
    const uint32_t rate = kOutRate;
    const uint32_t byteRate = kOutRate * 4;
    const uint16_t align = 4;
    const uint16_t bits = 16;
    const uint32_t fmtSize = 16;
    fseek(gWav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, gWav);
    fwrite(&riff, 4, 1, gWav);
    fwrite("WAVEfmt ", 1, 8, gWav);
    fwrite(&fmtSize, 4, 1, gWav);
    fwrite(&fmt, 2, 1, gWav);
    fwrite(&ch, 2, 1, gWav);
    fwrite(&rate, 4, 1, gWav);
    fwrite(&byteRate, 4, 1, gWav);
    fwrite(&align, 2, 1, gWav);
    fwrite(&bits, 2, 1, gWav);
    fwrite("data", 1, 4, gWav);
    fwrite(&dataBytes, 4, 1, gWav);
    fseek(gWav, 0, SEEK_END);
}

void Output()
{
    const uint16_t cntH = Io16(0x82);
    const bool master = (Io16(0x84) & 0x80) != 0;
    int left = 0;
    int right = 0;
    if (master)
    {
        for (int f = 0; f < 2; f++)
        {
            const int shift = 8 + 4 * f;
            const int vol = (cntH & (4 << f)) ? 2 : 1; // 100% / 50%
            const int s = gFifo[f].mSample * vol;
            if (cntH & (1 << shift))
            {
                right += s;
            }
            if (cntH & (2 << shift))
            {
                left += s;
            }
        }
    }
    // The DAC is 10 bits around SOUNDBIAS; scale the +-512 range to 16 bits.
    left = left < -512 ? -512 : (left > 511 ? 511 : left);
    right = right < -512 ? -512 : (right > 511 ? 511 : right);
    if (gFrameSamples < 1024)
    {
        gFrameBuf[2 * gFrameSamples] = static_cast<int16_t>(left * 64);
        gFrameBuf[2 * gFrameSamples + 1] = static_cast<int16_t>(right * 64);
        gFrameSamples++;
    }
}
} // namespace

void Apu_Init(bool enableOutput, const char* wavPath)
{
    if (enableOutput)
    {
        SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, kOutRate };
        gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (gStream)
        {
            SDL_ResumeAudioStreamDevice(gStream);
        }
        else
        {
            Log("no audio output: %s", SDL_GetError());
        }
    }
    if (wavPath)
    {
        gWav = fopen(wavPath, "wb");
        if (gWav)
        {
            WavHeader();
        }
    }
}

void Apu_Shutdown()
{
    if (gStream)
    {
        SDL_DestroyAudioStream(gStream);
        gStream = nullptr;
    }
    if (gWav)
    {
        WavHeader();
        fclose(gWav);
        gWav = nullptr;
    }
    if (gUnderruns)
    {
        Log("sound FIFOs ran dry %llu times", static_cast<unsigned long long>(gUnderruns));
    }
}

void Apu_ResetFifo(int f)
{
    gFifo[f].mRead = 0;
    gFifo[f].mCount = 0;
}

void Apu_PushFifo(int f, uint32_t word)
{
    Fifo& fifo = gFifo[f];
    for (int i = 0; i < 4; i++)
    {
        if (fifo.mCount >= 32)
        {
            return; // full: the hardware drops the rest
        }
        fifo.mData[(fifo.mRead + fifo.mCount) & 31] = static_cast<int8_t>(word >> (8 * i));
        fifo.mCount++;
    }
}

void Apu_OnTimerOverflow(int timer)
{
    if (timer > 1)
    {
        return;
    }
    const uint16_t cntH = Io16(0x82);
    for (int f = 0; f < 2; f++)
    {
        if (((cntH >> (10 + 4 * f)) & 1) != timer)
        {
            continue;
        }
        Fifo& fifo = gFifo[f];
        if (fifo.mCount > 0)
        {
            fifo.mSample = fifo.mData[fifo.mRead];
            fifo.mRead = (fifo.mRead + 1) & 31;
            fifo.mCount--;
        }
        else if (cntH & (3 << (8 + 4 * f)))
        {
            if (gUnderruns++ == 0)
            {
                Log("sound FIFO %c ran dry first at frame %llu line %d", 'A' + f,
                    static_cast<unsigned long long>(FrameCount()), CurrentLine());
            }
        }
        if (fifo.mCount <= 16)
        {
            Dma_OnFifoRequest(f);
        }
    }
}

void Apu_Advance(int cycles)
{
    gOutCycles += cycles;
    while (gOutCycles >= kCyclesPerOut)
    {
        gOutCycles -= kCyclesPerOut;
        Output();
    }
}

void Apu_EndFrame()
{
    const int bytes = gFrameSamples * 4;
    if (gStream)
    {
        // Keep at most ~0.15s queued: when the simulator runs faster than real time the excess is dropped.
        if (SDL_GetAudioStreamQueued(gStream) < kOutRate * 4 * 15 / 100)
        {
            SDL_PutAudioStreamData(gStream, gFrameBuf, bytes);
        }
    }
    if (gWav)
    {
        fwrite(gFrameBuf, 1, static_cast<size_t>(bytes), gWav);
        gWavBytes += static_cast<uint32_t>(bytes);
    }
    gFrameSamples = 0;
}
} // namespace sim
