#include <stdio.h>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "dialog_compiler.hpp"
#include "map_compiler.hpp"
#include "content_paths.hpp"

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

static bool IsFileNewer(const std::string& input,
                        const std::vector<std::string>& outputs)
{
    namespace fs = std::filesystem;

    // If the input doesn't exist, nothing to do
    if (!fs::exists(input))
    {
        printf("%s doesn't exist\n", input.c_str());
        return false;
    }

    auto inputTime = fs::last_write_time(input);

    for (const auto& out : outputs)
    {
        // If an output file is missing -> we must rebuild
        if (!fs::exists(out))
        {
            printf("%s doesn't exist\n", out.c_str());
            return true;
        }

        auto outTime = fs::last_write_time(out);

        // If input is newer than this output -> rebuild
        if (inputTime > outTime)
        {
            return true;
        }
    }

    // All outputs exist and are newer
    return false;
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

int main(int argc, char **argv)
{
    printf("cwd: %s\n", std::filesystem::current_path().string().c_str());

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
    for (const auto& mapName : mapNames)
    {
        CompileMap(contentPaths, mapName);
    }
    WriteMapIncludeFile(mapNames);
    WriteMapRegistry(mapNames);
    return 0;
}
