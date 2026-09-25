#include "mp3_decode.hpp"

#include "dr_mp3.h" // declarations only (guarded extern "C"); implementation lives in dr_mp3_impl.c

#include <memory>

namespace
{
    struct Drmp3Deleter final
    {
        void operator()(drmp3_int16* p) const { drmp3_free(p, nullptr); }
    };
}

PcmAudio DecodeMp3File(const std::string& path)
{
    PcmAudio result;

    drmp3_config config = {};
    drmp3_uint64 totalFrames = 0;
    std::unique_ptr<drmp3_int16, Drmp3Deleter> pcm(
        drmp3_open_file_and_read_pcm_frames_s16(path.c_str(), &config, &totalFrames, nullptr));

    if (pcm == nullptr || totalFrames == 0)
    {
        return result;
    }

    result.channels = static_cast<int>(config.channels);
    result.sampleRate = static_cast<int>(config.sampleRate);
    result.samples.assign(pcm.get(), pcm.get() + totalFrames * config.channels);

    return result;
}
