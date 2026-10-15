#pragma once

#include <stdint.h>

// One QOA-encoded frame living somewhere in the shared audio_blob.bin pool.
// offset/length alone fully describe it - dedup just means multiple tracks'
// frame arrays can point at the same {offset, length} pair.
typedef struct AudioFrameRef
{
    uint32_t offset;
    uint32_t length;
} AudioFrameRef;

typedef struct AudioTrackData
{
    const char* name;      // debug-HUD only
    uint32_t sampleRate;
    uint32_t totalSamples; // debug-HUD only (duration display)
    uint32_t frameCount;
    uint8_t loopEnabled;
    uint32_t loopStartFrame; // index into `frames`, meaningful only if loopEnabled
    const AudioFrameRef* frames;
} AudioTrackData;

// Must be >= AudioCompilerOptions::frameLenSamples (see audio_compiler.hpp) -
// the largest number of samples any single compiled QOA frame can hold.
// (Only host tooling sizes buffers by this now; the GBA decoder streams one
// 20-sample slice at a time, so frame size no longer affects ISR cost.)
#define AUDIO_MAX_FRAME_SAMPLES 1280

// How many simultaneous one-shot SFX can be mixed in at once, on top of the
// BGM and one looping ambience track (all software-mixed into one output).
#define AUDIO_MAX_SFX_SLOTS 4

void AudioManager_Init(void);
void AudioManager_PlayBGM(uint32_t trackId);
void AudioManager_PlayAmbience(uint32_t ambienceId);
void AudioManager_StopAmbience(void);
void AudioManager_StopBGM(void);
void AudioManager_PlaySFX(uint32_t sfxId);
void AudioManager_Update(void);

// Debug-HUD accessors. Kept as functions (rather than having callers include
// the generated audio_registry.h directly) so that ~900KB registry only gets
// pulled into audio_manager.cpp's translation unit, not duplicated into
// every TU that wants to display a track name/count.
uint32_t AudioManager_GetBgmCount(void);

// CPU cycles the audio VBlank ISR took (last call / max since reset). One
// VBlank period is 280896 cycles; this needs to stay a small fraction of it.
uint32_t AudioManager_GetIsrCyclesLast(void);
uint32_t AudioManager_GetIsrCyclesMax(void);
void AudioManager_ResetIsrCyclesMax(void);
uint32_t AudioManager_GetSfxCount(void);
const char* AudioManager_GetBgmName(uint32_t trackId);
const char* AudioManager_GetSfxName(uint32_t sfxId);

// Track lookup by name (the file stem used in AnodyneSharp, e.g. "overworld", "puddle_step"); -1 if unknown.
int32_t AudioManager_FindBgm(const char* name);
int32_t AudioManager_FindAmbience(const char* name);
int32_t AudioManager_FindSfx(const char* name);
