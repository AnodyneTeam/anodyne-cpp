#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct AudioCompilerOptions final
{
    bool force = false;   // ignore per-track freshness cache, recompile everything
    bool verbose = false; // print per-track compression stats
    uint32_t defaultSampleRate = 13379; // rate for items without sampleRateOverride - see main.cpp
    // QOA frame size (loop points snap to frame boundaries). The GBA decoder
    // streams per 20-sample slice, so this doesn't affect per-VBlank CPU
    // cost - see main.cpp's override and AUDIO_MAX_FRAME_SAMPLES in
    // audio_manager.h, both of which must match this.
    uint32_t frameLenSamples = 1280;
    // BGM plays at 8 bits on the GBA, so a track mastered 10-15dB below full
    // scale throws away 2+ of those bits (measured: quiet tracks like nexus
    // at ~14dB SNR). Each BGM track is scaled so its peak lands at
    // bgmNormalizePeakDb, but never boosted by more than bgmMaxNormalizeGainDb
    // - a cap so quiet/ambient tracks don't end up as loud as boss themes
    // (there's no runtime per-track volume to undo it). Never attenuates.
    // Not applied to ambience/SFX: those get summed together on FIFO B, and
    // boosting them would just clip the mix more.
    double bgmNormalizePeakDb = -0.5;
    double bgmMaxNormalizeGainDb = 9.0;
};

// One BGM/ambience/SFX item to compile. `sourcePath` points at the original
// .ogg/.mp3 (see ContentPaths::GetBgmPath/GetAmbiencePath/GetSfxPath).
struct AudioItemSpec final
{
    std::string name;     // used for the debug-HUD string
    std::string category; // "bgm"/"ambience"/"sfx" - namespaces the cache path so e.g.
                           // a BGM track and an ambience track sharing a name (both
                           // exist for "beach") don't collide on the same cache file
    std::string sourcePath;
    bool isMp3 = false;
    bool loop = false;
    uint32_t loopStartSample = 0;    // only used if `loop`; snapped to nearest frame boundary
    uint32_t sampleRateOverride = 0; // 0 = use AudioCompilerOptions::defaultSampleRate
};

// Result of compiling one AudioItemSpec - everything the registry writer
// needs to emit one AudioTrackData entry (see src/anodyne/audio_manager.h).
struct CompiledAudioTrack final
{
    std::string name;
    uint32_t sampleRate = 0;
    uint32_t totalSamples = 0;
    bool loopEnabled = false;
    uint32_t loopStartFrame = 0;
    std::vector<std::pair<uint32_t, uint32_t>> frames; // {offset, length} into the shared blob
};

// Encodes every AudioItemSpec to QOA (via a per-track on-disk cache, skipped
// when the cache is newer than the source - see IsFileNewer in main.cpp),
// then globally deduplicates identical encoded frames (by content hash)
// across every item compiled through this instance into one shared blob.
class AudioCompiler final
{
public:
    explicit AudioCompiler(const AudioCompilerOptions& options) : mOptions(options) {}

    CompiledAudioTrack CompileItem(const AudioItemSpec& spec);

    // Writes the concatenated, deduplicated frame pool to `path`.
    bool WriteBlob(const std::string& path) const;
    size_t BlobSizeBytes() const { return mBlob.size(); }
    size_t UniqueFrameCount() const { return mUniqueFrameCount; }
    size_t TotalFramesSeen() const { return mTotalFramesSeen; }

private:
    AudioCompilerOptions mOptions;
    std::vector<uint8_t> mBlob;
    // frame content hash -> offsets of (rare, possibly colliding) frames sharing
    // that hash; AddFrame memcmp's against candidates before trusting a match.
    std::unordered_map<uint64_t, std::vector<uint32_t>> mFrameHashToOffsets;
    size_t mTotalFramesSeen = 0;
    size_t mUniqueFrameCount = 0;

    // Adds `frameBytes` to the shared blob if an identical frame isn't
    // already present; returns {offset, length} either way.
    std::pair<uint32_t, uint32_t> AddFrame(const uint8_t* frameBytes, uint32_t length);
};
