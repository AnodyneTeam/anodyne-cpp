#include "map_compiler.hpp"
#include "lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include "lodepng.h"
#include <vector>
#include <cstdint>
#include <cstdio>
#include <unordered_set>


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

bool TileSet::Load(const std::string &pngPath)
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

    printf("Loaded PNG: %s (%u x %u), unique colors: %zu\n", pngPath.c_str(), width, height, CountUniqueColors(pixels));

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


bool MapCompiler::Load(const std::string& mapBGCSVPath, const std::string& mapTilesetPath)
{
    CsvMap csvMap;

   
    if (!csvMap.Load(mapBGCSVPath, mMap16))
    {
        printf("Failed to load map CSV: %s\n", mapBGCSVPath.c_str());
        return false;
    }

    if (!mTileSet16.Load(mapTilesetPath))
    {
        printf("Failed to load map tileset\n");
        return false;
    }

    return true;
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


void MapCompiler::Compile(const std::string& mapName)
{
    SaveMapAsPNG(mapName + "_16x16", mMap16, mTileSet16);

    TileSplitResult split16To8 = mTileSet16.Make8x8WithRemap();

    Array2D<uint32_t> map8;
    Build8x8Map(mMap16, split16To8, map8);

    SaveMapAsPNG(mapName + "_8x8", map8, split16To8.tiles8x8);
}

void MapCompiler::SaveMapAsPNG(const std::string& pngName, Array2D<uint32_t>& map, TileSet& tileSet)
{
    const uint32_t mapWidthTiles  = map.Width();
    const uint32_t mapHeightTiles = map.Height();

    const uint32_t tileWidth  = tileSet.GetTileWidth();
    const uint32_t tileHeight = tileSet.GetTileHeight();

    const uint32_t outWidth  = mapWidthTiles  * tileWidth;
    const uint32_t outHeight = mapHeightTiles * tileHeight;

    std::vector<RGBA> outPixels(outWidth * outHeight);

    // Flatten tileset info
    const uint32_t tilesX = tileSet.GetTiles().Width();
    const uint32_t tilesY = tileSet.GetTiles().Height();

    for(uint32_t my = 0; my < mapHeightTiles; ++my)
    {
        for(uint32_t mx = 0; mx < mapWidthTiles; ++mx)
        {
            uint32_t tileIndex = map.Get(mx, my);

            uint32_t tx = tileIndex % tilesX;
            uint32_t ty = tileIndex / tilesX;

            const RGBATile& tile = tileSet.GetTiles().Get(tx, ty);

            // Copy tile pixels into output image
            for(uint32_t y = 0; y < tileHeight; ++y)
            {
                for(uint32_t x = 0; x < tileWidth; ++x)
                {
                    const RGBA& src = tile.mData.Get(x, y);

                    uint32_t outX = mx * tileWidth  + x;
                    uint32_t outY = my * tileHeight + y;

                    outPixels[outY * outWidth + outX] = src;
                }
            }
        }
    }

    // Convert RGBA vector -> raw bytes for LodePNG
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
        printf("Map compiled to %s (%u x %u)\n", outPath.c_str(), outWidth, outHeight);
    }
}
