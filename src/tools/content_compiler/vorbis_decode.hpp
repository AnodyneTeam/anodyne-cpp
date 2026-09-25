#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PcmAudio final
{
    std::vector<int16_t> samples; // interleaved, `channels` per frame
    int channels = 0;
    int sampleRate = 0;
};

// Decodes an entire .ogg (Vorbis) file via stb_vorbis. Returns an empty
// PcmAudio (channels == 0) on failure.
PcmAudio DecodeVorbisFile(const std::string& path);
