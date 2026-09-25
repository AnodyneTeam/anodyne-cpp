#pragma once

#include "vorbis_decode.hpp" // for PcmAudio
#include <string>

// Decodes an entire .mp3 file via dr_mp3. Returns an empty PcmAudio
// (channels == 0) on failure.
PcmAudio DecodeMp3File(const std::string& path);
