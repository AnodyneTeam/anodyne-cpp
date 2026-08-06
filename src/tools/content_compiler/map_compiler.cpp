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

bool MapCompiler::Load(const std::string& mapBGCSVPath, const std::string& mapTilesetPath)
{
    CsvMap csvMap;

   
    if (!csvMap.Load(mapBGCSVPath, mMap))
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

void MapCompiler::Compile(const std::string& mapName)
{
    const uint32_t mapWidthTiles  = mMap.Width();
    const uint32_t mapHeightTiles = mMap.Height();

    const uint32_t tileWidth  = 16;
    const uint32_t tileHeight = 16;

    const uint32_t outWidth  = mapWidthTiles  * tileWidth;
    const uint32_t outHeight = mapHeightTiles * tileHeight;

    std::vector<RGBA> outPixels(outWidth * outHeight);

    // Flatten tileset info
    const uint32_t tilesX = mTileSet16.GetTiles().Width();
    const uint32_t tilesY = mTileSet16.GetTiles().Height();

    for(uint32_t my = 0; my < mapHeightTiles; ++my)
    {
        for(uint32_t mx = 0; mx < mapWidthTiles; ++mx)
        {
            uint32_t tileIndex = mMap.Get(mx, my);

            uint32_t tx = tileIndex % tilesX;
            uint32_t ty = tileIndex / tilesX;

            const RGBATile& tile = mTileSet16.GetTiles().Get(tx, ty);

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

    std::string outPath = mapName + ".png";

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
