#include <stdio.h>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cstdio>

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

    // Build 256-entry palette
    std::vector<uint16_t> palette(256, 0);
    size_t i = 0;

    for (uint32_t key : uniqueRGBA)
    {
        if (i >= 256)
            break;

        RGBA p;
        p.r = (key >> 24) & 0xFF;
        p.g = (key >> 16) & 0xFF;
        p.b = (key >> 8)  & 0xFF;
        p.a = (key)       & 0xFF;

        palette[i++] = GBA555(p);
    }

    return palette;
}

// ------------------------------------------------------------
// PALETTE SAVER
// ------------------------------------------------------------
void SavePaletteGBA(const std::string& headerName,
                    const std::vector<uint16_t>& palette)
{
    FILE* f = fopen(headerName.c_str(), "w");
    if (!f)
        return;

    fprintf(f, "// Auto-generated GBA palette\n");
    fprintf(f, "const unsigned short gTilesetPalette[256] = {\n");

    for (size_t i = 0; i < 256; ++i)
    {
        fprintf(f, "    0x%04X%s\n",
                palette[i],
                (i == 255 ? "" : ","));
    }

    fprintf(f, "};\n");
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
void SaveTileSetGBA(const std::string& headerName,
                    const std::vector<uint8_t>& tileData)
{
    FILE* f = fopen(headerName.c_str(), "w");
    if (!f)
        return;

    fprintf(f, "// Auto-generated GBA tileset (8bpp)\n");
    fprintf(f, "const unsigned char gTilesetData[%zu] = {\n",
            tileData.size());

    for (size_t i = 0; i < tileData.size(); ++i)
    {
        fprintf(f, "    0x%02X%s\n",
                tileData[i],
                (i + 1 == tileData.size() ? "" : ","));
    }

    fprintf(f, "};\n");
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



void SaveMapGBA(const std::string& headerName,
                const std::vector<uint16_t>& map,
                uint32_t width,
                uint32_t height)
{
    FILE* f = fopen(headerName.c_str(), "w");
    if (!f)
        return;

    fprintf(f, "// Auto-generated GBA tilemap\n");
    fprintf(f, "// %u x %u tiles\n\n", width, height);

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

    fclose(f);
}

// ------------------------------------------------------------
// MAP COMPILATION
// ------------------------------------------------------------
static void CompileMap(ContentPaths& contentPaths, const std::string& mapName)
{
    MapCompiler mc;
    auto ret = mc.Load(mapName,
                       contentPaths.GetMapBGCSVPath(mapName),
                       contentPaths.GetMapTilesetPath(mapName));

    if (!ret.success)
    {
        return;
    }

    auto palette = ExtractPaletteFromTileSet(ret.tiles8x8);
    SavePaletteGBA(mapName + "_palette.h", palette);

    auto tiles = ConvertTileSetTo8BPP(ret.tiles8x8, palette);
    SaveTileSetGBA(mapName + "_tiles.h", tiles);

    SaveMapGBA(mapName + "_map.h", ConvertMapToGBA(ret.map8), ret.map8.Width(), ret.map8.Height());

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

int main(int argc, char **argv)
{
    ContentPaths contentPaths("../../old/Content");

    //CompileDialog(contentPaths);
    //CompileMap(contentPaths, "STREET");
//    CompileMap(contentPaths, "OVERWORLD");
    CompileMap(contentPaths, "CLIFF");

    return 0;
}
