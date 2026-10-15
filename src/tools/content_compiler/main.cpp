#include <stdio.h>
#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "dialog_compiler.hpp"
#include "map_compiler.hpp"
#include "content_paths.hpp"
#include "file_utils.hpp"
#include "audio_compiler.hpp"
#include "sprite_compiler.hpp"

// ------------------------------------------------------------
// GBA RGB555 PACKER (THE ONLY ONE YOU SHOULD USE)
// ------------------------------------------------------------
static inline uint16_t GBA555(const RGBA& p)
{
    uint16_t r = (p.r >> 3) & 0x1F;
    uint16_t g = (p.g >> 3) & 0x1F;
    uint16_t b = (p.b >> 3) & 0x1F;

    return (r) | (g << 5) | (b << 10);
}

// ------------------------------------------------------------
// PALETTE EXTRACTION (UP TO 256 UNIQUE COLOURS)
// ------------------------------------------------------------
std::vector<uint16_t> ExtractPaletteFromTileSet(const TileSet& tileSet)
{
    std::unordered_set<uint32_t> uniqueRGBA;

    const auto& tiles = tileSet.GetTiles();
    const uint32_t tilesX = tiles.Width();
    const uint32_t tilesY = tiles.Height();

    for (uint32_t ty = 0; ty < tilesY; ++ty)
    {
        for (uint32_t tx = 0; tx < tilesX; ++tx)
        {
            const RGBATile& tile = tiles.Get(tx, ty);

            for (uint32_t y = 0; y < 8; ++y)
            {
                for (uint32_t x = 0; x < 8; ++x)
                {
                    const RGBA& p = tile.mData.Get(x, y);

                    // Ignore fully transparent pixels
                    if (p.a == 0)
                        continue;

                    uint32_t key =
                        (uint32_t(p.r) << 24) |
                        (uint32_t(p.g) << 16) |
                        (uint32_t(p.b) << 8)  |
                        (uint32_t(p.a));

                    uniqueRGBA.insert(key);
                }
            }
        }
    }

    std::vector<uint16_t> palette;
    palette.reserve(256);
    size_t i = 0;

    for (uint32_t key : uniqueRGBA)
    {
        if (i >= 256)
        {
            printf("ERROR OUT OF PAL!!!\n");
            break;
        }

        RGBA p;
        p.r = (key >> 24) & 0xFF;
        p.g = (key >> 16) & 0xFF;
        p.b = (key >> 8)  & 0xFF;
        p.a = (key)       & 0xFF;

        palette.push_back(GBA555(p));
    }

    return palette;
}

// ------------------------------------------------------------
// PALETTE SAVER
// ------------------------------------------------------------
void SavePaletteGBA(const std::string& nameSpace,
                    const std::string& headerName,
                    const std::vector<uint16_t>& palette)
{
    FILE* f = fopen(headerName.c_str(), "w");
    if (!f)
        return;

    fprintf(f, "namespace %s\n{\n", nameSpace.c_str());
    fprintf(f, "// Auto-generated GBA palette\n");
    fprintf(f, "const unsigned short gTilesetPaletteSizeElements = %d;\n\n", palette.size());
    fprintf(f, "const unsigned short gTilesetPalette[%zu] =\n{\n", palette.size());

    for(size_t i=0; i<palette.size(); i++)
    {
        fprintf(f, "    0x%04X%s\n",
                palette[i],
                (i == 255 ? "" : ","));
    }

    fprintf(f, "};\n"); // Array end
    fprintf(f, "}\n"); // Namespace end
    fclose(f);
}

// ------------------------------------------------------------
// TILE → PALETTE INDEX
// ------------------------------------------------------------
uint8_t FindPaletteIndex8BPP(const RGBA& p,
                             const std::vector<uint16_t>& palette)
{
    uint16_t c = GBA555(p);

    for (size_t i = 0; i < palette.size(); ++i)
        if (palette[i] == c)
            return (uint8_t)i;

    return 0;
}

// ------------------------------------------------------------
// TILESET → 8BPP TILE DATA
// ------------------------------------------------------------
std::vector<uint8_t> ConvertTileSetTo8BPP(const TileSet& tileSet,
                                          const std::vector<uint16_t>& palette)
{
    const auto& tiles = tileSet.GetTiles();
    const uint32_t tilesX = tiles.Width();
    const uint32_t tilesY = tiles.Height();
    const uint32_t tileCount = tilesX * tilesY;

    std::vector<uint8_t> out(tileCount * 64);
    size_t pos = 0;

    for (uint32_t ty = 0; ty < tilesY; ++ty)
    {
        for (uint32_t tx = 0; tx < tilesX; ++tx)
        {
            const RGBATile& tile = tiles.Get(tx, ty);

            for (uint32_t y = 0; y < 8; ++y)
            {
                for (uint32_t x = 0; x < 8; ++x)
                {
                    out[pos++] = FindPaletteIndex8BPP(tile.mData.Get(x, y), palette);
                }
            }
        }
    }

    return out;
}

// ------------------------------------------------------------
// TILESET SAVER
// ------------------------------------------------------------
void SaveTileSetGBA(const std::string& nameSpace,
                    const std::string& headerName,
                    const std::vector<uint8_t>& tileData)
{
    FILE* f = fopen(headerName.c_str(), "w");
    if (!f)
        return;

    fprintf(f, "namespace %s\n{\n", nameSpace.c_str());
    fprintf(f, "// Auto-generated GBA tileset (8bpp)\n");
    fprintf(f, "const unsigned int gTilesetDataSizeElements = %zu;\n", tileData.size());
    fprintf(f, "const unsigned char gTilesetData[%zu] = \n{\n", tileData.size());

    for (size_t i = 0; i < tileData.size(); ++i)
    {
        fprintf(f, "    0x%02X%s\n",
                tileData[i],
                (i + 1 == tileData.size() ? "" : ","));
    }

    fprintf(f, "};\n");
    fprintf(f, "}\n");
    fclose(f);
}

std::vector<uint16_t> ConvertMapToGBA(const Array2D<uint32_t>& map)
{
    const uint32_t w = map.Width();
    const uint32_t h = map.Height();

    std::vector<uint16_t> out(w * h);

    size_t pos = 0;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            uint32_t packed = map.Get(x, y);

            uint16_t tileIndex = packed & 0xFFFF;
            TileVariant variant = (TileVariant)((packed >> 16) & 0xFF);

            uint16_t hflip = (variant == VAR_H || variant == VAR_HV) ? 1 : 0;
            uint16_t vflip = (variant == VAR_V || variant == VAR_HV) ? 1 : 0;

            uint16_t entry =
                (tileIndex & 0x03FF) |   // GBA tile index (0–1023)
                (hflip << 10)        |   // bit 10 = H flip
                (vflip << 11);           // bit 11 = V flip

            out[pos++] = entry;
        }
    }

    return out;
}



void SaveMapGBA(const std::string& nameSpace,
                const std::string& headerName,
                const std::vector<uint16_t>& map,
                uint32_t width,
                uint32_t height)
{
    FILE* f = fopen(headerName.c_str(), "w");
    if (!f)
        return;

    fprintf(f, "namespace %s\n{\n", nameSpace.c_str());
    fprintf(f, "// Auto-generated GBA tilemap\n");

    fprintf(f, "const unsigned int gMapWidth  = %u;\n", width);
    fprintf(f, "const unsigned int gMapHeight = %u;\n\n", height);

    fprintf(f, "const unsigned short gMapData[%u] = {\n", width * height);

    for (size_t i = 0; i < map.size(); ++i)
    {
        fprintf(f, "    0x%04X%s\n",
                map[i],
                (i + 1 == map.size() ? "" : ","));
    }

    fprintf(f, "};\n");
    fprintf(f, "}\n");

    fclose(f);
}

// ------------------------------------------------------------
// MAP COMPILATION
// ------------------------------------------------------------
static void CompileMap(ContentPaths& contentPaths, const std::string& mapName)
{

    std::string mapBgCsvPath = contentPaths.GetMapBGCSVPath(mapName);
    std::string mapTilesetPath = contentPaths.GetMapTilesetPath(mapName);

    std::string palHeader = mapName + "_pal.h";
    std::string tilesHeader = mapName + "_tiles.h";
    std::string mapHeader = mapName + "_map.h";

    if (IsFileNewer(mapBgCsvPath, {palHeader, tilesHeader, mapHeader}) ||
        IsFileNewer(mapTilesetPath, {palHeader, tilesHeader, mapHeader}))
    {
        printf("Compiling map: %s\n", mapName.c_str());
    }
    else
    {
        printf("Map up to date: %s\n", mapName.c_str());
        return;
    }

    MapCompiler mc;
    auto ret = mc.Load(mapName,
                       mapBgCsvPath,
                       mapTilesetPath);

    if (!ret.success)
    {
        return;
    }

    auto palette = ExtractPaletteFromTileSet(ret.tiles8x8);
    SavePaletteGBA(mapName, palHeader, palette);

    auto tiles = ConvertTileSetTo8BPP(ret.tiles8x8, palette);
    SaveTileSetGBA(mapName, tilesHeader, tiles);

    SaveMapGBA(mapName, mapHeader, ConvertMapToGBA(ret.map8), ret.map8.Width(), ret.map8.Height());

    printf("Map compilation complete: %s\n", mapName.c_str());
}


static void CompileDialog(ContentPaths& contentPaths)
{
    DialogCompiler dc;
    if (dc.Load(contentPaths.GetDialogPath("EN")))
    {
        dc.Compile();
        dc.DebugPrint();
    }
    else
    {
        printf("Dialog OVER!\n");
    }
}

void WriteMapIncludeFile(const std::vector<std::string>& mapNames)
{
    FILE* f = fopen("maps.h", "w");
    if (!f) return;

    fprintf(f, "// Auto-generated map include file\n\n");

    for (const auto& name : mapNames)
    {
        fprintf(f, "#include \"%s_pal.h\"\n", name.c_str());
        fprintf(f, "#include \"%s_tiles.h\"\n", name.c_str());
        fprintf(f, "#include \"%s_map.h\"\n\n", name.c_str());
    }

    fclose(f);
}

void WriteMapRegistry(const std::vector<std::string>& mapNames)
{
    FILE* f = fopen("map_registry.h", "w");
    if (!f) return;

    fprintf(f, "// Auto-generated map registry\n\n");

    // Includes
    for (const auto& name : mapNames)
    {
        fprintf(f, "#include \"%s_pal.h\"\n", name.c_str());
        fprintf(f, "#include \"%s_tiles.h\"\n", name.c_str());
        fprintf(f, "#include \"%s_map.h\"\n\n", name.c_str());
    }

    // Registry array
    fprintf(f, "static const MapEntry kMaps[] =\n{\n");

    for (const auto& name : mapNames)
    {
        fprintf(f,
            "    {\n"
            "        { %s::gTilesetPaletteSizeElements, %s::gTilesetPalette },\n"
            "        { %s::gTilesetDataSizeElements,    %s::gTilesetData    },\n"
            "        { %s::gMapWidth,                  %s::gMapHeight, %s::gMapData }\n"
            "    },\n",
            name.c_str(), name.c_str(),
            name.c_str(), name.c_str(),
            name.c_str(), name.c_str(), name.c_str()
        );
    }

    fprintf(f, "};\n\n");
    fprintf(f, "static const uint32_t kMapCount = sizeof(kMaps) / sizeof(kMaps[0]);\n");

    fclose(f);
}

// ------------------------------------------------------------
// AUDIO COMPILATION
// ------------------------------------------------------------
static std::string EscapeCString(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return out;
}

static void WriteAudioFrameArrays(const char* varPrefix, const std::vector<CompiledAudioTrack>& tracks, FILE* f)
{
    for (size_t i = 0; i < tracks.size(); i++)
    {
        fprintf(f, "static const AudioFrameRef %s_%zu_Frames[] = {\n", varPrefix, i);
        for (const auto& frame : tracks[i].frames)
        {
            fprintf(f, "    { %uu, %uu },\n", frame.first, frame.second);
        }
        if (tracks[i].frames.empty())
        {
            fprintf(f, "    { 0u, 0u },\n"); // keep the array non-empty (failed decode)
        }
        fprintf(f, "};\n");
    }
}

static void WriteAudioTrackArray(const char* arrayName, const char* varPrefix,
                                  const std::vector<CompiledAudioTrack>& tracks, FILE* f)
{
    fprintf(f, "static const AudioTrackData %s[] = {\n", arrayName);
    for (size_t i = 0; i < tracks.size(); i++)
    {
        const CompiledAudioTrack& t = tracks[i];
        fprintf(f, "    { \"%s\", %uu, %uu, %zuu, %u, %uu, %s_%zu_Frames },\n", EscapeCString(t.name).c_str(),
                t.sampleRate, t.totalSamples, t.frames.size(), t.loopEnabled ? 1 : 0, t.loopStartFrame, varPrefix,
                i);
    }
    fprintf(f, "};\n");
    fprintf(f, "static const uint32_t %sCount = sizeof(%s) / sizeof(%s[0]);\n\n", arrayName, arrayName, arrayName);
}

static void WriteAudioRegistry(uint32_t outputSampleRate, const std::vector<CompiledAudioTrack>& bgm,
                                const std::vector<CompiledAudioTrack>& ambience,
                                const std::vector<CompiledAudioTrack>& sfx)
{
    FILE* idx = fopen("audio_block_index.h", "w");
    if (idx)
    {
        fprintf(idx, "// Auto-generated audio frame index\n\n");
        WriteAudioFrameArrays("kBgm", bgm, idx);
        WriteAudioFrameArrays("kAmbience", ambience, idx);
        WriteAudioFrameArrays("kSfx", sfx, idx);
        fclose(idx);
    }

    // NOTE: relies on whoever includes this having already included
    // src/anodyne/audio_manager.h (for AudioTrackData/AudioFrameRef) - same
    // convention map_registry.h uses for MapEntry.
    FILE* reg = fopen("audio_registry.h", "w");
    if (reg)
    {
        fprintf(reg, "// Auto-generated audio registry\n\n");
        // Mix/output rate; every item is at this rate or half of it (see CompileAllAudio).
        fprintf(reg, "#define AUDIO_OUTPUT_SAMPLE_RATE %uu\n\n", outputSampleRate);
        fprintf(reg, "#include \"audio_block_index.h\"\n\n");
        WriteAudioTrackArray("kBgmTracks", "kBgm", bgm, reg);
        WriteAudioTrackArray("kAmbienceTracks", "kAmbience", ambience, reg);
        WriteAudioTrackArray("kSfxClips", "kSfx", sfx, reg);
        fclose(reg);
    }
}

struct AudioCliOptions final
{
    bool force = false;
    bool verbose = false;
    bool mapsOnly = false;
    bool audioOnly = false;
    bool spritesOnly = false;
};

static AudioCliOptions ParseAudioCliOptions(int argc, char** argv)
{
    AudioCliOptions options;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--force")
        {
            options.force = true;
        }
        else if (arg == "--verbose")
        {
            options.verbose = true;
        }
        else if (arg == "--maps-only")
        {
            options.mapsOnly = true;
        }
        else if (arg == "--sprites-only")
        {
            options.spritesOnly = true;
        }
        else if (arg == "--audio-only")
        {
            options.audioOnly = true;
        }
        else
        {
            printf("Unknown option: %s\n", arg.c_str());
        }
    }
    return options;
}

static void CompileAllAudio(ContentPaths& contentPaths, const AudioCliOptions& cliOptions)
{
    AudioCompilerOptions options;
    options.force = cliOptions.force;
    options.verbose = cliOptions.verbose;
    // The GBA side mixes every stream in software into one 9-bit output at
    // kOutputSampleRate (both DirectSound FIFOs on one timer - see
    // audio_manager.cpp). Each item is compiled at either that rate or
    // exactly half of it (upsampled 2x while mixing); nothing else works.
    // 26758Hz output: 13.4kHz bandwidth, and a whole multiple of 16 samples
    // per VBlank (448) as the playback path requires (ValidateGbaSampleRate).
    // Items default to half rate (13379Hz, 6.7kHz bandwidth): all BGM at
    // full rate would be ~47MB, far over the 32MB cart limit, so only
    // kFullRateBgm get it.
    const uint32_t kOutputSampleRate = 26758;
    options.defaultSampleRate = kOutputSampleRate / 2;

    // BGM tracks compiled at the full output rate: the most treble-heavy
    // tracks (measured: strong content in 6.7-13.4kHz, which half rate cuts
    // off) plus ones that sounded worst at half rate, chosen to fit the
    // 32MB ROM (~0.3MB headroom left - trim this list to make room for
    // other content).
    const std::vector<std::string> kFullRateBgm = {
        "ending", "bedroom", "hotel-boss", "circus", "sacrificial", "cell", "gameover", "hotel", "boss"
    };
    // 1280-sample QOA frames. (The old per-frame "click" was the GBA side
    // decoding a whole frame inside the VBlank ISR and starving the sound
    // DMA - fixed by streaming per-slice decode in audio_decode_core.cpp,
    // not by frame size.) See AUDIO_MAX_FRAME_SAMPLES in audio_manager.h,
    // must match.
    options.frameLenSamples = 1280;

    // Per-track loop-start overrides (in samples at the track's own encode
    // rate) - no original AnodyneSharp loop-point metadata exists anywhere in
    // this repo, so everything defaults to looping the whole track from 0.
    std::vector<std::pair<std::string, uint32_t>> loopOverrides = {};

    auto findOverride = [](const std::vector<std::pair<std::string, uint32_t>>& overrides,
                            const std::string& name) -> uint32_t
    {
        for (const auto& o : overrides)
        {
            if (o.first == name)
            {
                return o.second;
            }
        }
        return 0;
    };

    std::vector<std::string> bgmNames = {
        "anodyne_TRAILER_1", "apartment", "apartment-boss", "beach", "bedroom", "bedroom-boss", "blank", "blue",
        "boss", "briar-fight", "cell", "circus", "circus-boss", "cliff", "crowd", "crowd_boss", "drawer", "ending",
        "fields", "forest", "gameover", "go", "happy", "happy_alt", "hotel", "hotel_alt", "hotel-boss", "mitra",
        "nexus", "overworld", "redcave", "redcave-boss", "redsea", "sacrificial", "sagefight", "space", "street",
        "suburb", "suburb_alt", "terminal", "terminal_alt", "title", "windmill"
    };
    std::vector<std::string> ambienceNames = { "beach", "windmill" };
    // {category subdirectory, clip name} - old/Content/sfx/<category>/<clip>.mp3
    std::vector<std::pair<std::string, std::string>> sfxClips = {
        { "1", "big_door_locked" }, { "1", "big_wave" }, { "1", "broom_hit" }, { "1", "bubble_2" },
        { "1", "bubble_3" }, { "1", "bubble_loop" }, { "1", "button_down" }, { "1", "button_up" },
        { "1", "cicada_chirp" }, { "1", "cross2" }, { "1", "cross3" }, { "1", "cross4" }, { "1", "dash_pad_1" },
        { "1", "dash_pad_2" }, { "1", "dialogue_bloop" }, { "1", "dustmaid_alert" }, { "1", "dustpoof" },
        { "1", "elevator_close" }, { "1", "elevator_open" }, { "1", "enter_door" }, { "1", "fall_1" },
        { "1", "fall_in_hole" }, { "1", "floor_crack" }, { "1", "gasguy_shoot" }, { "1", "get_small_health" },
        { "1", "gettreasure" }, { "1", "hit_ground_1" }, { "1", "keyget" }, { "1", "ladder_step_1" },
        { "1", "ladder_step_2" }, { "1", "noise_step_1" }, { "1", "on_off_laser_shoot" }, { "1", "open" },
        { "1", "player_hit_1" }, { "1", "player_jump_down" }, { "1", "player_jump_up" }, { "1", "puddle_down" },
        { "1", "puddle_up" }, { "1", "pushblock" }, { "1", "redboss_death" }, { "1", "redboss_moan" },
        { "1", "red_cave_rise" }, { "1", "sb_dash" }, { "1", "sb_hurt" }, { "1", "sb_split" },
        { "1", "shieldy-hit" }, { "1", "slasher_atk" }, { "1", "small_wave" }, { "1", "sparkle_3" },
        { "1", "spring_bounce" }, { "1", "stream" }, { "1", "sun_guy_charge" }, { "1", "sun_guy_death_long" },
        { "1", "sun_guy_death_short" }, { "1", "sun_guy_scream2" }, { "1", "sun_guy_scream" },
        { "1", "swing_broom_1" }, { "1", "swing_broom_2" }, { "1", "swing_broom_3" }, { "1", "talk_2" },
        { "1", "talk_death" }, { "1", "teleguy_down" }, { "1", "teleguy_up" }, { "1", "teleport_down" },
        { "1", "teleport_up" }, { "1", "unlock" }, { "1", "wb_hit_ground" }, { "1", "wb_moan_2" },
        { "1", "wb_moan" }, { "1", "wb_shoot" }, { "1", "wb_tap_ground" }, { "10", "hit_slime" },
        { "10", "laser_charge" }, { "10", "laser_fire" }, { "2", "4sht_shoot" }, { "2", "bubble_1" },
        { "2", "dog_bark" }, { "2", "flame_pillar" }, { "2", "gasguy_move" }, { "2", "laser-pew" },
        { "2", "menu_select" }, { "2", "mover_die" }, { "2", "mover_move" }, { "2", "pause_sound" },
        { "2", "puddle_step" }, { "2", "rat_move" }, { "2", "slime_shoot" }, { "2", "sparkle_1" },
        { "2", "sparkle_2" }, { "2", "talk_1" }, { "2", "talk_3" }, { "3", "4sht_pop" }, { "3", "dog_dash" },
        { "3", "dust_explode" }, { "3", "menu_move" }, { "3", "sf_move" }, { "3", "water_step" },
        { "4", "bubble_triple" }, { "4", "fireball" }, { "4", "shieldy_ineffective" }, { "5", "dialogue_blip" },
        { "5", "hit_wall" }, { "5", "sb_ball_appear" }, { "5", "slime_walk" }, { "8", "briar_shine" },
        { "8", "slime_splash" }
    };

    AudioCompiler compiler(options);

    std::vector<CompiledAudioTrack> bgmTracks;
    for (const auto& name : bgmNames)
    {
        AudioItemSpec spec;
        spec.name = name;
        spec.category = "bgm";
        spec.sourcePath = contentPaths.GetBgmPath(name);
        spec.isMp3 = false;
        spec.loop = true;
        spec.loopStartSample = findOverride(loopOverrides, name);
        if (std::find(kFullRateBgm.begin(), kFullRateBgm.end(), name) != kFullRateBgm.end())
        {
            spec.sampleRateOverride = kOutputSampleRate;
        }
        bgmTracks.push_back(compiler.CompileItem(spec));
    }

    std::vector<CompiledAudioTrack> ambienceTracks;
    for (const auto& name : ambienceNames)
    {
        AudioItemSpec spec;
        spec.name = name;
        spec.category = "ambience";
        spec.sourcePath = contentPaths.GetAmbiencePath(name);
        spec.isMp3 = false;
        spec.loop = true;
        spec.loopStartSample = findOverride(loopOverrides, name);
        ambienceTracks.push_back(compiler.CompileItem(spec));
    }

    std::vector<CompiledAudioTrack> sfxClipsCompiled;
    for (const auto& [category, clip] : sfxClips)
    {
        AudioItemSpec spec;
        spec.name = clip;
        spec.category = "sfx_" + category;
        spec.sourcePath = contentPaths.GetSfxPath(category, clip);
        spec.isMp3 = true;
        spec.loop = false;
        sfxClipsCompiled.push_back(compiler.CompileItem(spec));
    }

    // The GBA mixer only knows "output rate" or "half of it" (anything else
    // would silently play at the wrong speed) - fail the build instead.
    for (const auto* list : { &bgmTracks, &ambienceTracks, &sfxClipsCompiled })
    {
        for (const auto& t : *list)
        {
            if (t.sampleRate != kOutputSampleRate && t.sampleRate * 2 != kOutputSampleRate)
            {
                printf("ERROR: %s compiled at %u Hz - must be %u or %u\n", t.name.c_str(), t.sampleRate,
                       kOutputSampleRate, kOutputSampleRate / 2);
                std::exit(1);
            }
        }
    }

    compiler.WriteBlob("audio_blob.bin");
    WriteAudioRegistry(kOutputSampleRate, bgmTracks, ambienceTracks, sfxClipsCompiled);

    printf("Audio compilation complete: %zu BGM, %zu ambience, %zu SFX -> %zu bytes (%zu/%zu unique frames)\n",
           bgmTracks.size(), ambienceTracks.size(), sfxClipsCompiled.size(), compiler.BlobSizeBytes(),
           compiler.UniqueFrameCount(), compiler.TotalFramesSeen());
}

int main(int argc, char **argv)
{
    printf("cwd: %s\n", std::filesystem::current_path().string().c_str());

    AudioCliOptions cliOptions = ParseAudioCliOptions(argc, argv);

    ContentPaths contentPaths("../../../old/Content");

    std::vector<std::string> mapNames = {
        "APARTMENT",
        "BEDROOM",
        "BLUE",
        "CELL",
        "CLIFF",
        "DEBUG",
        "FIELDS",
        "GO",
        "HOTEL",
        "OVERWORLD",
        "REDSEA",
        "STREET",
        "TERMINAL",
        "BEACH",
        "BLANK",
        "BOSSRUSH",
        "CIRCUS",
        "CROWD",
        "DRAWER",
        "FOREST",
        "HAPPY",
        "NEXUS",
        "REDCAVE",
        "SPACE",
        "SUBURB",
        "WINDMILL"
    };

    //CompileDialog(contentPaths);
    if (!cliOptions.audioOnly && !cliOptions.spritesOnly)
    {
        for (const auto& mapName : mapNames)
        {
            CompileMap(contentPaths, mapName);
        }
        WriteMapIncludeFile(mapNames);
        WriteMapRegistry(mapNames);
    }

    if (!cliOptions.mapsOnly && !cliOptions.audioOnly)
    {
        SpriteCompiler spriteCompiler;
        spriteCompiler.WriteHeaders(spriteCompiler.CompileAll(contentPaths));
    }

    if (!cliOptions.mapsOnly && !cliOptions.spritesOnly)
    {
        CompileAllAudio(contentPaths, cliOptions);
    }

    return 0;
}
