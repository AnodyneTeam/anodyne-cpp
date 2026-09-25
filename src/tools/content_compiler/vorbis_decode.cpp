#include "vorbis_decode.hpp"

#include <cstdlib>
#include <memory>

// stb_vorbis.c is compiled as its own translation unit (see CMakeLists.txt);
// it ships no separate header, so we declare the one entry point we need
// ourselves, matching its C linkage.
extern "C" int stb_vorbis_decode_filename(const char* filename, int* channels,
                                           int* sample_rate, short** output);

PcmAudio DecodeVorbisFile(const std::string& path)
{
    PcmAudio result;

    int channels = 0;
    int sampleRate = 0;
    short* pcm = nullptr;

    int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &sampleRate, &pcm);
    std::unique_ptr<short, decltype(&std::free)> pcmGuard(pcm, &std::free);

    if (frames <= 0 || pcm == nullptr)
    {
        return result;
    }

    result.channels = channels;
    result.sampleRate = sampleRate;
    result.samples.assign(pcm, pcm + static_cast<size_t>(frames) * static_cast<size_t>(channels));

    return result;
}
