#include "map_compiler.hpp"
#include "lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include "lodepng.h"
#include <vector>
#include <cstdint>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <math.h>

bool RGBATile::EqualsHFlip(const RGBATile& other) const
{
    const uint32_t w = mData.Width();
    const uint32_t h = mData.Height();

    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const RGBA& a = mData.Get(x, y);
            const RGBA& b = other.mData.Get(w - 1 - x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
                return false;
        }
    }
    return true;
}

bool RGBATile::EqualsVFlip(const RGBATile& other) const
{
    const uint32_t w = mData.Width();
    const uint32_t h = mData.Height();

    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const RGBA& a = mData.Get(x, y);
            const RGBA& b = other.mData.Get(x, h - 1 - y);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
                return false;
        }
    }
    return true;
}

bool RGBATile::EqualsHVFlip(const RGBATile& other) const
{
    const uint32_t w = mData.Width();
    const uint32_t h = mData.Height();

    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const RGBA& a = mData.Get(x, y);
            const RGBA& b = other.mData.Get(w - 1 - x, h - 1 - y);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
                return false;
        }
    }
    return true;
}


bool load_png_to_vector(const char* filename,
                        std::vector<RGBA>& outPixels,
                        unsigned& width,
                        unsigned& height)
{
    unsigned char* image = nullptr;
    unsigned error = lodepng_decode32_file(&image, &width, &height, filename);

    if(error)
    {
        printf("error %u: %s\n", error, lodepng_error_text(error));
        return false;
    }

    const size_t pixelCount = width * height;
    outPixels.resize(pixelCount);

    // Convert RGBA bytes → uint32_t
    for(size_t i = 0; i < pixelCount; ++i)
    {
        outPixels[i].r = image[i*4 + 0];
        outPixels[i].g = image[i*4 + 1];
        outPixels[i].b = image[i*4 + 2];
        outPixels[i].a = image[i*4 + 3];
    }

    free(image);
    return true;
}

size_t CountUniqueColors(const std::vector<RGBA>& pixels)
{
    std::unordered_set<uint32_t> unique;

    unique.reserve(pixels.size()); // avoid rehashing

    for(const RGBA& p : pixels)
    {
        uint32_t key =
            (uint32_t(p.r) << 24) |
            (uint32_t(p.g) << 16) |
            (uint32_t(p.b) << 8)  |
            (uint32_t(p.a));

        unique.insert(key);
    }

    return unique.size();
}

TileSet::TileSet(uint32_t tileWidth, uint32_t tileHeight)
    : mTileWidth(tileWidth), mTileHeight(tileHeight)
{
}

bool TileSet::Load(const std::string &pngPath, size_t& coloursCount)
{
    std::vector<RGBA> pixels;

    uint32_t width = 0;
    uint32_t height = 0;
    if (!load_png_to_vector(pngPath.c_str(), pixels, width, height))
    {
        printf("Failed to load PNG: %s\n", pngPath.c_str());
        return false;
    }

    if (width % mTileWidth != 0 || height % mTileHeight != 0)
    {
        printf("PNG size does not match tile size\n");
        return false;
    }

    coloursCount = CountUniqueColors(pixels);

    const uint32_t tilesX = width / mTileWidth;
    const uint32_t tilesY = height / mTileHeight;

    mTiles.Resize(tilesX, tilesY);
    for (uint32_t ty = 0; ty < tilesY; ++ty)
    {
        for (uint32_t tx = 0; tx < tilesX; ++tx)
        {
            mTiles.Get(tx, ty).SetSize(mTileWidth, mTileHeight);

            for (uint32_t y = 0; y < mTileHeight; ++y)
            {
                for (uint32_t x = 0; x < mTileWidth; ++x)
                {
                    const uint32_t pixelX = tx * mTileWidth + x;
                    const uint32_t pixelY = ty * mTileHeight + y;
                    const RGBA &pixel = pixels[pixelY * width + pixelX];
                    mTiles.Get(tx, ty).mData.Set(x, y, pixel);
                }
            }
        }
    }

    return true;
}

uint32_t TileSet::AppendTiles(const std::vector<RGBATile>& tiles)
{
    const uint32_t width = mTiles.Width();
    const uint32_t firstIndex = width * mTiles.Height();
    const uint32_t rows = (static_cast<uint32_t>(tiles.size()) + width - 1) / width;
    mTiles.Resize(width, mTiles.Height() + rows);

    for (uint32_t i = 0; i < width * rows; ++i)
    {
        RGBATile padded;
        padded.SetSize(mTileWidth, mTileHeight); // transparent
        const uint32_t index = firstIndex + i;
        mTiles.Get(index % width, index / width) = i < tiles.size() ? tiles[i] : padded;
    }
    return firstIndex;
}

void TileSet::ClearTile(uint32_t index)
{
    const uint32_t tx = index % mTiles.Width();
    const uint32_t ty = index / mTiles.Width();
    if (ty >= mTiles.Height())
    {
        return;
    }

    RGBATile& tile = mTiles.Get(tx, ty);
    for (uint32_t y = 0; y < mTileHeight; ++y)
    {
        for (uint32_t x = 0; x < mTileWidth; ++x)
        {
            tile.mData.Set(x, y, RGBA{ 0, 0, 0, 0 });
        }
    }
}

TileSplitResult TileSet::Make8x8WithRemap() const
{
    TileSplitResult result;
    result.tiles8x8 = TileSet(8, 8);

    const uint32_t oldTilesX = mTiles.Width();
    const uint32_t oldTilesY = mTiles.Height();

    const uint32_t newTilesX = oldTilesX * 2;
    const uint32_t newTilesY = oldTilesY * 2;

    result.tiles8x8.mTiles.Resize(newTilesX, newTilesY);

    result.remap16to8.resize(oldTilesX * oldTilesY);

    for (uint32_t ty = 0; ty < oldTilesY; ++ty)
    {
        for (uint32_t tx = 0; tx < oldTilesX; ++tx)
        {
            const uint32_t oldIndex = ty * oldTilesX + tx;

            std::array<uint32_t,4> subTiles;

            uint32_t baseX = tx * 2;
            uint32_t baseY = ty * 2;

            subTiles[0] = (baseY + 0) * newTilesX + (baseX + 0); // top-left
            subTiles[1] = (baseY + 0) * newTilesX + (baseX + 1); // top-right
            subTiles[2] = (baseY + 1) * newTilesX + (baseX + 0); // bottom-left
            subTiles[3] = (baseY + 1) * newTilesX + (baseX + 1); // bottom-right

            result.remap16to8[oldIndex] = subTiles;

            // Now actually split the tile
            const RGBATile& oldTile = mTiles.Get(tx, ty);

            for (uint32_t sy = 0; sy < 2; ++sy)
            {
                for (uint32_t sx = 0; sx < 2; ++sx)
                {
                    uint32_t newTx = baseX + sx;
                    uint32_t newTy = baseY + sy;

                    RGBATile& newTile = result.tiles8x8.mTiles.Get(newTx, newTy);
                    newTile.SetSize(8, 8);

                    for (uint32_t y = 0; y < 8; ++y)
                    {
                        for (uint32_t x = 0; x < 8; ++x)
                        {
                            const RGBA& p = oldTile.mData.Get(sx*8 + x, sy*8 + y);
                            newTile.mData.Set(x, y, p);
                        }
                    }
                }
            }
        }
    }

    return result;
}


static RGBA GetPixelVariant(const RGBATile& tile,
                            uint32_t x, uint32_t y,
                            TileVariant v)
{
    const uint32_t w = tile.mData.Width();
    const uint32_t h = tile.mData.Height();

    uint32_t sx = x;
    uint32_t sy = y;

    switch (v)
    {
    case VAR_IDENTITY:
        break;

    case VAR_H:
        sx = w - 1 - x;
        break;

    case VAR_V:
        sy = h - 1 - y;
        break;

    case VAR_HV:
        sx = w - 1 - x;
        sy = h - 1 - y;
        break;
    }

    return tile.mData.Get(sx, sy);
}


inline uint32_t PackRGBA(const RGBA& p)
{
    return (uint32_t(p.r) << 24) |
           (uint32_t(p.g) << 16) |
           (uint32_t(p.b) << 8)  |
           (uint32_t(p.a));
}

struct CanonicalTileKey
{
    std::vector<uint32_t> pixels; // canonical ordering
};

struct CanonicalTileKeyHash
{
    size_t operator()(const CanonicalTileKey& k) const
    {
        size_t h = 0;
        for (uint32_t v : k.pixels)
        {
            h ^= std::hash<uint32_t>()(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

inline bool operator==(const CanonicalTileKey& a, const CanonicalTileKey& b)
{
    return a.pixels == b.pixels;
}

static void BuildCanonicalKey(const RGBATile& tile,
                              CanonicalTileKey& outKey,
                              TileVariant& outVariant)
{
    const uint32_t w = tile.mData.Width();
    const uint32_t h = tile.mData.Height();

    const TileVariant variants[] =
    {
        VAR_IDENTITY,
        VAR_H,
        VAR_V,
        VAR_HV
    };

    CanonicalTileKey bestKey;
    TileVariant bestVar = VAR_IDENTITY;
    bool first = true;

    for (TileVariant v : variants)
    {
        CanonicalTileKey key;
        key.pixels.resize(w * h);

        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                RGBA p = GetPixelVariant(tile, x, y, v);
                key.pixels[y * w + x] = PackRGBA(p);
            }
        }

        if (first || key.pixels < bestKey.pixels)
        {
            bestKey = key;
            bestVar = v;
            first = false;
        }
    }

    outKey = bestKey;
    outVariant = bestVar;
}


static TileVariant InverseVariant(TileVariant v)
{
    switch (v)
    {
    case VAR_IDENTITY: return VAR_IDENTITY;
    case VAR_H:        return VAR_H;
    case VAR_V:        return VAR_V;
    case VAR_HV:       return VAR_HV;
    }
    return VAR_IDENTITY;
}

TileDedupResult TileSet::Deduplicate() const
{
    TileDedupResult result;

    const uint32_t tilesX = mTiles.Width();
    const uint32_t tilesY = mTiles.Height();
    const uint32_t tileCount = tilesX * tilesY;

    result.remap8toUnique.resize(tileCount);

    std::unordered_map<CanonicalTileKey, uint32_t, CanonicalTileKeyHash> uniqueMap;
    std::vector<RGBATile> uniqueTiles;

    for (uint32_t ty = 0; ty < tilesY; ++ty)
    {
        for (uint32_t tx = 0; tx < tilesX; ++tx)
        {
            const RGBATile& tile = mTiles.Get(tx, ty);
            uint32_t oldIndex = ty * tilesX + tx;

            CanonicalTileKey key;
            TileVariant varToCanonical;
            BuildCanonicalKey(tile, key, varToCanonical);

            TileVariant varFromCanonical = InverseVariant(varToCanonical);

            auto it = uniqueMap.find(key);
            if (it == uniqueMap.end())
            {
                uint32_t newIndex = uniqueTiles.size();
                uniqueMap.emplace(key, newIndex);

                RGBATile canonical;
                canonical.SetSize(mTileWidth, mTileHeight);
                for (uint32_t y = 0; y < mTileHeight; ++y)
                {
                    for (uint32_t x = 0; x < mTileWidth; ++x)
                    {
                        RGBA p = GetPixelVariant(tile, x, y, varToCanonical);
                        canonical.mData.Set(x, y, p);
                    }
                }

                uniqueTiles.push_back(canonical);

                result.remap8toUnique[oldIndex] = { newIndex, varFromCanonical };
            }
            else
            {
                uint32_t newIndex = it->second;
                result.remap8toUnique[oldIndex] = { newIndex, varFromCanonical };
            }
        }
    }

    result.tilesUnique = TileSet(mTileWidth, mTileHeight);
    result.tilesUnique.mTiles.Resize(uniqueTiles.size(), 1);

    for (uint32_t i = 0; i < uniqueTiles.size(); ++i)
        result.tilesUnique.mTiles.Get(i, 0) = uniqueTiles[i];

    return result;
}


MapCompileResult MapCompiler::Load(const std::string& mapName, const std::string& mapBGCSVPath, const std::string& bg2CSVPath,
                                   const std::string& fgCSVPath, const std::string& mapTilesetPath,
                                   const std::string& scrollTexturePath)
{
    MapCompileResult ret;

    // Get CSV data
    CsvMap csvMap;   
    if (!csvMap.Load(mapBGCSVPath, mMap16))
    {
        printf("Failed to load map CSV: %s\n", mapBGCSVPath.c_str());
        return ret;
    }

    // Get 16x16 tile images
    if (!mTileSet16.Load(mapTilesetPath, ret.mTileSetColoursCount))
    {
        printf("Failed to load map tileset\n");
        return ret;
    }

    // Map.cs: tile 0 is invisible. It must be fully transparent so empty BG2/FG cells show what is underneath.
    mTileSet16.ClearTile(0);

    ret.layers[0] = mMap16;
    csvMap.Load(bg2CSVPath, ret.layers[1]); // optional layers: a missing file leaves the layer empty
    csvMap.Load(fgCSVPath, ret.layers[2]);

    // Save the map PNG - build with 16x16 tile indices and 16x16 tile images
    SaveMapAsPNG(mapName + "_16x16", mMap16, mTileSet16);

    // Split the tileset into 8x8 entries
    TileSplitResult split16To8 = mTileSet16.Make8x8WithRemap();

    // A scrolling background (see PlayState.SetBackground): its image repeats every 160px, so only the top-left
    // 160x160 matters. Its 8x8 tiles join the map's tile set so they share the palette and de-duplication.
    uint32_t scrollBase = 0;
    bool haveScroll = false;
    if (!scrollTexturePath.empty())
    {
        TileSet scrollSet(8, 8);
        size_t scrollColours = 0;
        if (scrollSet.Load(scrollTexturePath, scrollColours))
        {
            std::vector<RGBATile> scrollTiles;
            for (uint32_t ty = 0; ty < 20; ++ty)
            {
                for (uint32_t tx = 0; tx < 20; ++tx)
                {
                    RGBATile tile;
                    tile.SetSize(8, 8);
                    if (tx < scrollSet.GetTiles().Width() && ty < scrollSet.GetTiles().Height())
                    {
                        tile = scrollSet.GetTiles().Get(tx, ty);
                    }
                    scrollTiles.push_back(tile);
                }
            }
            scrollBase = split16To8.tiles8x8.AppendTiles(scrollTiles);
            haveScroll = true;
        }
        else
        {
            printf("Failed to load scrolling background %s\n", scrollTexturePath.c_str());
        }
    }

    // Expand the map to the new 8x8 indices (1 entry becomes 4)
    Array2D<uint32_t> map8;
    Build8x8Map(mMap16, split16To8, map8);

    // Get the unique 8x8 tiles
    TileDedupResult dedup = split16To8.tiles8x8.Deduplicate();

    // Fix the map the new the new tile indices and varients
    Apply8x8Remap(map8, dedup.remap8toUnique);

    // Debug save the map and tileset
    SaveMapAsPNG(mapName + "_8x8", map8, dedup.tilesUnique);
    SaveTileSetAsPNG(mapName + "_8x8_tiles", dedup.tilesUnique);

    ret.tile16Count = static_cast<uint32_t>(split16To8.remap16to8.size());
    for (const auto& sub : split16To8.remap16to8)
    {
        for (uint32_t index : sub)
        {
            const DedupEntry& e = dedup.remap8toUnique[index];
            ret.tile16Entries.push_back((uint32_t(e.variant) << 16) | e.newIndex);
        }
    }

    if (haveScroll)
    {
        for (uint32_t i = 0; i < 400; ++i)
        {
            const DedupEntry& e = dedup.remap8toUnique[scrollBase + i];
            ret.scrollPattern.push_back((uint32_t(e.variant) << 16) | e.newIndex);
        }
    }

    ret.map8 = map8;
    ret.tiles8x8 = dedup.tilesUnique;
    ret.m8x8TileCountBeforeDeDupe = split16To8.tiles8x8.GetTiles().Width() * split16To8.tiles8x8.GetTiles().Height();
    ret.success = true;
    return ret;
}

void MapCompiler::Build8x8Map(const Array2D<uint32_t>& map16,
                              const TileSplitResult& split,
                              Array2D<uint32_t>& outMap8)
{
    const uint32_t w16 = map16.Width();
    const uint32_t h16 = map16.Height();

    const uint32_t w8 = w16 * 2;
    const uint32_t h8 = h16 * 2;

    outMap8.Resize(w8, h8);

    for (uint32_t my = 0; my < h16; ++my)
    {
        for (uint32_t mx = 0; mx < w16; ++mx)
        {
            uint32_t tile16 = map16.Get(mx, my);

            const auto& sub = split.remap16to8[tile16];

            // Write 4 tiles into the 8×8 map
            outMap8.Set(mx*2 + 0, my*2 + 0, sub[0]); // TL
            outMap8.Set(mx*2 + 1, my*2 + 0, sub[1]); // TR
            outMap8.Set(mx*2 + 0, my*2 + 1, sub[2]); // BL
            outMap8.Set(mx*2 + 1, my*2 + 1, sub[3]); // BR
        }
    }
}

void MapCompiler::Apply8x8Remap(Array2D<uint32_t>& map8,
                                const std::vector<DedupEntry>& remap)
{
    const uint32_t w = map8.Width();
    const uint32_t h = map8.Height();

    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            uint32_t oldIndex = map8.Get(x, y);
            const DedupEntry& e = remap[oldIndex];

            // pack: [variant << 16] | newIndex
            uint32_t packed = (uint32_t(e.variant) << 16) | e.newIndex;

            map8.Set(x, y, packed);
        }
    }
}



void MapCompiler::SaveMapAsPNG(const std::string& pngName,
                               Array2D<uint32_t>& map,
                               TileSet& tileSet)
{
    const uint32_t mapWidthTiles  = map.Width();
    const uint32_t mapHeightTiles = map.Height();

    const uint32_t tileWidth  = tileSet.GetTileWidth();
    const uint32_t tileHeight = tileSet.GetTileHeight();

    const uint32_t outWidth  = mapWidthTiles  * tileWidth;
    const uint32_t outHeight = mapHeightTiles * tileHeight;

    std::vector<RGBA> outPixels(outWidth * outHeight);

    const uint32_t tilesX = tileSet.GetTiles().Width();
    const uint32_t tilesY = tileSet.GetTiles().Height();

    for(uint32_t my = 0; my < mapHeightTiles; ++my)
    {
        for(uint32_t mx = 0; mx < mapWidthTiles; ++mx)
        {
            uint32_t entry = map.Get(mx, my);

            uint32_t tileIndex = entry & 0xFFFF;
            uint32_t variant   = (entry >> 16) & 0xFF;   // FULL transform ID

            uint32_t tx = tileIndex % tilesX;
            uint32_t ty = tileIndex / tilesX;

            const RGBATile& tile = tileSet.GetTiles().Get(tx, ty);

            for(uint32_t y = 0; y < tileHeight; ++y)
            {
                for(uint32_t x = 0; x < tileWidth; ++x)
                {
                    // Apply transform (flip/rotation)
                    RGBA src = GetPixelVariant(tile, x, y, (TileVariant)variant);

                    uint32_t outX = mx * tileWidth  + x;
                    uint32_t outY = my * tileHeight + y;

                    outPixels[outY * outWidth + outX] = src;
                }
            }
        }
    }

    std::vector<uint8_t> raw(outWidth * outHeight * 4);
    for(size_t i = 0; i < outPixels.size(); ++i)
    {
        raw[i*4 + 0] = outPixels[i].r;
        raw[i*4 + 1] = outPixels[i].g;
        raw[i*4 + 2] = outPixels[i].b;
        raw[i*4 + 3] = outPixels[i].a;
    }

    std::string outPath = pngName + ".png";

    unsigned error = lodepng_encode32_file(outPath.c_str(), raw.data(), outWidth, outHeight);
    if(error)
    {
        printf("PNG encode error %u: %s\n", error, lodepng_error_text(error));
    }
    else
    {
        //printf("Map compiled to %s (%u x %u)\n", outPath.c_str(), outWidth, outHeight);
    }
}

void MapCompiler::SaveTileSetAsPNG(const std::string& pngName, const TileSet& tileSet)
{
    const uint32_t tileWidth  = tileSet.GetTileWidth();
    const uint32_t tileHeight = tileSet.GetTileHeight();

    const uint32_t tilesX = tileSet.GetTiles().Width();
    const uint32_t tilesY = tileSet.GetTiles().Height();
    const uint32_t tileCount = tilesX * tilesY;

    // Make the output as square as possible
    uint32_t tilesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(tileCount)));
    uint32_t rows = (tileCount + tilesPerRow - 1) / tilesPerRow;

    const uint32_t outWidth  = tilesPerRow * tileWidth;
    const uint32_t outHeight = rows * tileHeight;

    std::vector<RGBA> outPixels(outWidth * outHeight);

    // Fill with transparent black
    for (auto& px : outPixels)
        px = {0,0,0,0};

    // Write each tile
    for (uint32_t i = 0; i < tileCount; ++i)
    {
        uint32_t tx = i % tilesX;
        uint32_t ty = i / tilesX;

        const RGBATile& tile = tileSet.GetTiles().Get(tx, ty);

        uint32_t outTileX = i % tilesPerRow;
        uint32_t outTileY = i / tilesPerRow;

        uint32_t baseX = outTileX * tileWidth;
        uint32_t baseY = outTileY * tileHeight;

        for (uint32_t y = 0; y < tileHeight; ++y)
        {
            for (uint32_t x = 0; x < tileWidth; ++x)
            {
                const RGBA& src = tile.mData.Get(x, y);

                uint32_t outX = baseX + x;
                uint32_t outY = baseY + y;

                outPixels[outY * outWidth + outX] = src;
            }
        }
    }

    // Convert to raw bytes
    std::vector<uint8_t> raw(outWidth * outHeight * 4);
    for(size_t i = 0; i < outPixels.size(); ++i)
    {
        raw[i*4 + 0] = outPixels[i].r;
        raw[i*4 + 1] = outPixels[i].g;
        raw[i*4 + 2] = outPixels[i].b;
        raw[i*4 + 3] = outPixels[i].a;
    }

    std::string outPath = pngName + ".png";

    unsigned error = lodepng_encode32_file(outPath.c_str(), raw.data(), outWidth, outHeight);
    if(error)
    {
        printf("PNG encode error %u: %s\n", error, lodepng_error_text(error));
    }
    else
    {
        //printf("TileSet saved to %s (%u x %u)\n", outPath.c_str(), outWidth, outHeight);
    }
}
