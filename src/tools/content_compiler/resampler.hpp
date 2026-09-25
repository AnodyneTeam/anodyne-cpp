#pragma once

#include <cstdint>
#include <vector>

// Downmixes interleaved multi-channel PCM to mono (simple channel average),
// then resamples to targetRate with a band-limited (windowed-sinc) filter.
std::vector<int16_t> DownmixAndResample(const std::vector<int16_t>& interleaved,
                                         int channels, int srcRate, int targetRate);
