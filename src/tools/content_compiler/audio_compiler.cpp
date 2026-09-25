#include "audio_compiler.hpp"

#include "file_utils.hpp"
#include "mp3_decode.hpp"
#include "qoa_decl.hpp"
#include "resampler.hpp"
#include "vorbis_decode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
    // FNV-1a, 64-bit.
    uint64_t HashFrameBytes(const uint8_t* data, uint32_t length)
    {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t i = 0; i < length; i++)
        {
            h ^= data[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    // qoa_encode_frame packs its 8-byte frame header as:
    //   channels<<56 | samplerate<<32 | samples<<16 | frame_size
    // (see qoa.h) - both fields we need are self-describing in the stream, so
    // neither the per-track cache nor the final blob need extra framing.
    uint64_t PeekFrameHeader(const uint8_t* data)
    {
        uint64_t header = 0;
        for (int i = 0; i < 8; i++)
        {
            header = (header << 8) | data[i];
        }
        return header;
    }

    uint32_t PeekFrameSize(const uint8_t* data) { return static_cast<uint32_t>(PeekFrameHeader(data) & 0xFFFF); }
    uint32_t PeekFrameSamples(const uint8_t* data)
    {
        return static_cast<uint32_t>((PeekFrameHeader(data) >> 16) & 0xFFFF);
    }

    uint32_t SumFrameSamples(const std::vector<uint8_t>& encoded)
    {
        uint32_t total = 0;
        uint32_t pos = 0;
        while (pos < encoded.size())
        {
            uint32_t size = PeekFrameSize(&encoded[pos]);
            if (size == 0 || pos + size > encoded.size())
            {
                break;
            }
            total += PeekFrameSamples(&encoded[pos]);
            pos += size;
        }
        return total;
    }

    // The GBA side (audio_manager.cpp) re-arms each sound DMA once per
    // VBlank from a fresh buffer, and the FIFO DMA fetches in 16-sample
    // bursts - so playback is only sample-exact if the timer rate gives a
    // whole multiple of 16 samples per VBlank (280896 cycles). Otherwise the
    // DMA's read-ahead drifts against the refill and glitches periodically.
    // Must mirror ComputeTimerDivisor() in audio_decode_core.cpp.
    // Valid: 10512 (176), 13379 (224), 18157 (304), 21024 (352).
    bool ValidateGbaSampleRate(uint32_t sampleRate, const std::string& name)
    {
        uint32_t divisor = (16777216u + sampleRate / 2) / sampleRate;
        if (280896u % divisor == 0 && (280896u / divisor) % 16 == 0)
        {
            return true;
        }
        printf("ERROR: %s: sample rate %u Hz gives %.3f samples per VBlank - must be a whole multiple of 16 "
               "(valid: 10512, 13379, 18157, 21024)\n",
               name.c_str(), sampleRate, 280896.0 / divisor);
        return false;
    }

    // Bump whenever the decode -> PCM -> QOA pipeline changes output (e.g.
    // resampler changes), so stale per-track caches aren't silently reused -
    // the cache is otherwise only invalidated by the SOURCE file changing.
    const int kPcmPipelineVersion = 3; // 2: windowed-sinc resampler, 3: capped BGM peak normalization

    std::string CachePath(const AudioItemSpec& spec, uint32_t sampleRate, uint32_t frameLen)
    {
        return "audio_cache/" + spec.category + "_" + spec.name + "_" + std::to_string(sampleRate) + "_" +
               std::to_string(frameLen) + "_v" + std::to_string(kPcmPipelineVersion) + ".qoacache";
    }

    // Scales `pcm` so its peak reaches targetPeakDb (dBFS), boosting by at
    // most maxGainDb and never attenuating - see AudioCompilerOptions.
    void NormalizeBgmPeak(std::vector<int16_t>& pcm, double targetPeakDb, double maxGainDb, const std::string& name,
                          bool verbose)
    {
        int peak = 0;
        for (int16_t v : pcm)
        {
            peak = std::max(peak, std::abs(static_cast<int>(v)));
        }
        if (peak == 0)
        {
            return;
        }
        double gain = 32767.0 * std::pow(10.0, targetPeakDb / 20.0) / peak;
        gain = std::min(gain, std::pow(10.0, maxGainDb / 20.0));
        if (gain <= 1.0)
        {
            return;
        }
        for (int16_t& v : pcm)
        {
            double scaled = std::round(static_cast<double>(v) * gain);
            v = static_cast<int16_t>(std::clamp(scaled, -32768.0, 32767.0));
        }
        if (verbose)
        {
            printf("  %-24s normalized +%.1f dB\n", name.c_str(), 20.0 * std::log10(gain));
        }
    }

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            return {};
        }
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    void WriteFile(const std::string& path, const std::vector<uint8_t>& data)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
}

std::pair<uint32_t, uint32_t> AudioCompiler::AddFrame(const uint8_t* frameBytes, uint32_t length)
{
    mTotalFramesSeen++;
    uint64_t hash = HashFrameBytes(frameBytes, length);

    auto& candidates = mFrameHashToOffsets[hash];
    for (uint32_t offset : candidates)
    {
        if (offset + length <= mBlob.size() && std::memcmp(&mBlob[offset], frameBytes, length) == 0)
        {
            return {offset, length};
        }
    }

    uint32_t offset = static_cast<uint32_t>(mBlob.size());
    mBlob.insert(mBlob.end(), frameBytes, frameBytes + length);
    candidates.push_back(offset);
    mUniqueFrameCount++;
    return {offset, length};
}

bool AudioCompiler::WriteBlob(const std::string& path) const
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }
    f.write(reinterpret_cast<const char*>(mBlob.data()), static_cast<std::streamsize>(mBlob.size()));
    return true;
}

CompiledAudioTrack AudioCompiler::CompileItem(const AudioItemSpec& spec)
{
    uint32_t sampleRate = spec.sampleRateOverride ? spec.sampleRateOverride : mOptions.defaultSampleRate;
    if (!ValidateGbaSampleRate(sampleRate, spec.name))
    {
        std::exit(1); // fail the build - a bad rate plays with periodic glitches, not an obvious error
    }
    uint32_t frameLen = mOptions.frameLenSamples;

    std::string cachePath = CachePath(spec, sampleRate, frameLen);
    std::vector<uint8_t> encoded;
    uint32_t totalSamples = 0;

    bool needsEncode = mOptions.force || IsFileNewer(spec.sourcePath, {cachePath});
    if (!needsEncode)
    {
        encoded = ReadFile(cachePath);
        needsEncode = encoded.empty();
        if (!needsEncode)
        {
            totalSamples = SumFrameSamples(encoded);
        }
    }

    if (needsEncode)
    {
        PcmAudio decoded = spec.isMp3 ? DecodeMp3File(spec.sourcePath) : DecodeVorbisFile(spec.sourcePath);
        if (decoded.channels == 0)
        {
            printf("WARNING: failed to decode %s\n", spec.sourcePath.c_str());
            return CompiledAudioTrack{spec.name, sampleRate, 0, false, 0, {}};
        }

        std::vector<int16_t> mono =
            DownmixAndResample(decoded.samples, decoded.channels, decoded.sampleRate, sampleRate);
        totalSamples = static_cast<uint32_t>(mono.size());

        if (spec.category == "bgm")
        {
            NormalizeBgmPeak(mono, mOptions.bgmNormalizePeakDb, mOptions.bgmMaxNormalizeGainDb, spec.name,
                             mOptions.verbose);
        }

        qoa_desc qoa = {};
        qoa.channels = 1;
        qoa.samplerate = sampleRate;
        qoa.samples = totalSamples;
        // Same initial LMS weights qoa_encode() itself seeds with (see qoa.h) -
        // helps prediction in the first few ms of a track.
        qoa.lms[0].weights[0] = 0;
        qoa.lms[0].weights[1] = 0;
        qoa.lms[0].weights[2] = -(1 << 13);
        qoa.lms[0].weights[3] = (1 << 14);
        for (int i = 0; i < QOA_LMS_LEN; i++)
        {
            qoa.lms[0].history[i] = 0;
        }

        std::vector<uint8_t> scratch(qoa_max_frame_size(&qoa));
        for (uint32_t i = 0; i < totalSamples; i += frameLen)
        {
            uint32_t len = std::min(frameLen, totalSamples - i);
            unsigned int written = qoa_encode_frame(&mono[i], &qoa, len, scratch.data());
            encoded.insert(encoded.end(), scratch.data(), scratch.data() + written);
        }

        std::filesystem::create_directories("audio_cache");
        WriteFile(cachePath, encoded);
    }

    CompiledAudioTrack track;
    track.name = spec.name;
    track.sampleRate = sampleRate;
    track.totalSamples = totalSamples;

    uint32_t pos = 0;
    uint32_t nominalSampleOffset = 0;
    uint32_t loopStartFrameIdx = 0;
    bool foundLoopFrame = !spec.loop;

    while (pos < encoded.size())
    {
        uint32_t frameSize = PeekFrameSize(&encoded[pos]);
        if (frameSize == 0 || pos + frameSize > encoded.size())
        {
            break; // corrupt/truncated cache - defensive, shouldn't happen
        }

        auto [offset, length] = AddFrame(&encoded[pos], frameSize);
        track.frames.push_back({offset, length});

        if (!foundLoopFrame && nominalSampleOffset + frameLen > spec.loopStartSample)
        {
            loopStartFrameIdx = static_cast<uint32_t>(track.frames.size()) - 1;
            foundLoopFrame = true;
        }

        nominalSampleOffset += frameLen;
        pos += frameSize;
    }

    track.loopEnabled = spec.loop;
    track.loopStartFrame = spec.loop ? loopStartFrameIdx : 0;

    if (mOptions.verbose)
    {
        double bitsPerSample = totalSamples > 0 ? (double)encoded.size() * 8.0 / totalSamples : 0.0;
        printf("  %-24s %8u samples  %8zu bytes  %.2f bits/sample  %4zu frames\n", spec.name.c_str(),
               totalSamples, encoded.size(), bitsPerSample, track.frames.size());
    }

    return track;
}
