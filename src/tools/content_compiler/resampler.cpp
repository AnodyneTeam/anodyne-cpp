#include "resampler.hpp"

#include <algorithm>
#include <cmath>

std::vector<int16_t> DownmixAndResample(const std::vector<int16_t>& interleaved,
                                         int channels, int srcRate, int targetRate)
{
    if (channels <= 0 || srcRate <= 0 || targetRate <= 0)
    {
        return {};
    }

    size_t frameCount = interleaved.size() / static_cast<size_t>(channels);

    std::vector<int16_t> mono(frameCount);
    for (size_t i = 0; i < frameCount; i++)
    {
        if (channels == 1)
        {
            mono[i] = interleaved[i];
        }
        else
        {
            int32_t sum = 0;
            for (int c = 0; c < channels; c++)
            {
                sum += interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
            }
            mono[i] = static_cast<int16_t>(sum / channels);
        }
    }

    if (srcRate == targetRate || frameCount == 0)
    {
        return mono;
    }

    // Windowed-sinc (Blackman) low-pass resampler. The previous plain linear
    // interpolation had no anti-aliasing filter, so everything between the
    // new Nyquist (~5.2kHz at 10512Hz) and the source's ~22kHz folded back
    // down as inharmonic noise - measured up to ~20dB SNR loss on bright
    // tracks (cymbals/hats/synths), e.g. cell, happy, hotel.
    const double ratio = static_cast<double>(srcRate) / static_cast<double>(targetRate);
    const double cutoff = 0.5 * 0.95 * std::min(1.0, 1.0 / ratio); // cycles per INPUT sample
    const double zeroCrossings = 16.0;
    const double halfWidth = zeroCrossings / (2.0 * cutoff); // kernel half-width, in input samples
    const double pi = 3.14159265358979323846;

    size_t outFrameCount = static_cast<size_t>(static_cast<double>(frameCount) / ratio);
    std::vector<int16_t> out(outFrameCount);
    for (size_t i = 0; i < outFrameCount; i++)
    {
        double center = static_cast<double>(i) * ratio;
        long first = static_cast<long>(std::ceil(center - halfWidth));
        long last = static_cast<long>(std::floor(center + halfWidth));
        double acc = 0.0;
        double weightSum = 0.0;
        for (long j = first; j <= last; j++)
        {
            double t = static_cast<double>(j) - center;
            double x = 2.0 * cutoff * t;
            double sinc = (x == 0.0) ? 1.0 : std::sin(pi * x) / (pi * x);
            double w = 0.42 + 0.5 * std::cos(pi * t / halfWidth) + 0.08 * std::cos(2.0 * pi * t / halfWidth);
            double h = sinc * w;
            weightSum += h;
            if (j >= 0 && static_cast<size_t>(j) < frameCount)
            {
                acc += h * static_cast<double>(mono[static_cast<size_t>(j)]);
            }
        }
        // Normalize by the full kernel's sum (unity DC gain); samples past
        // either end of the track count as silence.
        double v = std::round(acc / weightSum);
        out[i] = static_cast<int16_t>(std::clamp(v, -32768.0, 32767.0));
    }

    return out;
}
