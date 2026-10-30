#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <array>
#include <stdint.h>

template<typename T>
class Array2D final
{
public:
    Array2D() = default;

    void Resize(size_t width, size_t height)
    {
        mWidth = width;
        mHeight = height;
        mData.resize(width * height);
    }

    void Set(size_t x, size_t y, T value)
    {
        if (x >= mWidth || y >= mHeight)
        {
            return;
        }
        mData[y * mWidth + x] = value;
    }

    const T& Get(size_t x, size_t y) const
    {
        if (x >= mWidth || y >= mHeight)
        {
            static T dummy{};
            return dummy;
        }
        return mData[y * mWidth + x];
    }

    T& Get(size_t x, size_t y)
    {
        if (x >= mWidth || y >= mHeight)
        {
            static T dummy{};
            return dummy;
        }
        return mData[y * mWidth + x];
    }

    uint32_t Width() const { return mWidth; }
    uint32_t Height() const { return mHeight; }

private:
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    std::vector<T> mData;
};

class CsvMap final
{
public:
    bool Load(const std::string& csvPath, Array2D<uint32_t>& map)
    {
        std::ifstream csvStream(csvPath);
        if (!csvStream.is_open())
        {
            return false;
        }

        std::vector<std::vector<std::string>> rows;
        std::string line;
        while (std::getline(csvStream, line))
        {
            std::vector<std::string> row;
            std::stringstream lineStream(line);
            std::string cell;

            while (std::getline(lineStream, cell, ','))
            {
                row.push_back(cell);
            }
            rows.push_back(row);
        }

        if (rows.empty())
        {
            return false;
        }

        // Validate width
        const uint32_t width = static_cast<uint32_t>(rows[0].size());
        for (const auto& row : rows)
        {
            if (row.size() != width)
            {
                printf("CSV error: inconsistent row width\n");
                return false;
            }
        }

        const uint32_t height = static_cast<uint32_t>(rows.size());
        
        // Fill it
        size_t index = 0;

        map.Resize(width, height);
        for (size_t y = 0; y < height; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                map.Set(x, y, static_cast<uint32_t>(std::stoi(rows[y][x])));
            }
        }

        return true;
    }

};

struct RGBA final
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

struct RGBATile final
{
    void SetSize(uint32_t width, uint32_t height)
    {
        mData.Resize(width, height);
    }

    Array2D<RGBA> mData;

    bool operator==(const RGBATile& other) const
    {
        if (mData.Width() != other.mData.Width()) return false;
        if (mData.Height() != other.mData.Height()) return false;

        for (uint32_t y = 0; y < mData.Height(); ++y)
        {
            for (uint32_t x = 0; x < mData.Width(); ++x)
            {
                const RGBA& a = mData.Get(x, y);
                const RGBA& b = other.mData.Get(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
                    return false;
            }
        }
        return true;
    }

    bool EqualsHFlip(const RGBATile& other) const;
    bool EqualsVFlip(const RGBATile& other) const;
    bool EqualsHVFlip(const RGBATile& other) const;

};

struct RGBATileHash
{
    size_t operator()(const RGBATile& tile) const
    {
        size_t h = 0;
        for (uint32_t y = 0; y < tile.mData.Height(); ++y)
        {
            for (uint32_t x = 0; x < tile.mData.Width(); ++x)
            {
                const RGBA& p = tile.mData.Get(x, y);
                uint32_t packed =
                    (uint32_t(p.r) << 24) |
                    (uint32_t(p.g) << 16) |
                    (uint32_t(p.b) << 8)  |
                    (uint32_t(p.a));

                h ^= std::hash<uint32_t>()(packed) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
        }
        return h;
    }
};


enum TileVariant : uint8_t
{
    VAR_IDENTITY = 0,
    VAR_H        = 1,
    VAR_V        = 2,
    VAR_HV       = 3,
};

struct TileSplitResult;
struct TileDedupResult;

class TileSet final
{
public:
    TileSet(uint32_t tileWidth, uint32_t tileHeight);
    bool Load(const std::string& pngPath, size_t& coloursCount);
    const Array2D<RGBATile>& GetTiles() const
    {
        return mTiles;
    }

    // Makes every pixel of one tile transparent (AnodyneSharp never draws 16x16 tile 0, whatever the PNG holds).
    void ClearTile(uint32_t index);

    // Adds tiles after the existing ones (growing the grid by whole rows, padded with empty tiles); returns the index
    // of the first added tile, i.e. width * old height.
    uint32_t AppendTiles(const std::vector<RGBATile>& tiles);
    // The same for a one-row set (a de-duplicated one): it just gets wider, no padding.
    uint32_t AppendTilesToRow(const std::vector<RGBATile>& tiles);

    TileSplitResult Make8x8WithRemap() const;
    TileDedupResult Deduplicate() const;

    uint32_t GetTileWidth() const { return mTileWidth; }
    uint32_t GetTileHeight() const { return mTileHeight; }
private:
    uint32_t mTileWidth = 0;
    uint32_t mTileHeight = 0;
    Array2D<RGBATile> mTiles;
};

struct DedupEntry final
{
    uint32_t newIndex = 0;
    uint8_t  variant = 0; // TileVariant
};

struct TileDedupResult final
{
    TileDedupResult()
        : tilesUnique(8, 8)
    {

    }
    TileSet tilesUnique;
    std::vector<DedupEntry> remap8toUnique;
};

struct TileSplitResult final
{
    TileSplitResult()
     : tiles8x8(8, 8)
    {

    }

    TileSet tiles8x8;
    std::vector<std::array<uint32_t,4>> remap16to8;
};

// A map's animated tile (Maps/<MAP>/TileAnims/*.dat, TileData.GetAnimData): on the BG layer, 16x16 tile `tile` gets the
// texture's 16x16 frames drawn over it in turn, `fps` a second.
struct TileAnimSpec final
{
    uint32_t tile = 0;
    int fps = 0;
    std::vector<int> frames;
    std::string texturePath;
};

// One compiled: on the BG layer its four 8x8 tiles are drawn from VRAM slots of their own (TL, TR, BL, BR, unique tile
// indices; unflipped) that the frames are copied into; frame N is animFrameTiles[(firstFrame + N) * 4 ..] (the tile
// with the texture frame over it). The other layers keep the plain tile.
struct TileAnimOut final
{
    uint32_t tile = 0;
    int fps = 0;
    uint32_t slots[4] = {};
    uint32_t firstFrame = 0;
    uint32_t frameCount = 0;
};

struct MapCompileResult final
{
    MapCompileResult()
        : tiles8x8(8, 8)
    {

    }
    bool success = false;
    uint32_t m8x8TileCountBeforeDeDupe = 0;
    size_t mTileSetColoursCount = 0;
    Array2D<uint32_t> map8;
    TileSet tiles8x8;

    // The BG, BG2 and FG layers as 16x16 tile indices (Width() == 0 when the map has no such layer).
    Array2D<uint32_t> layers[3];
    // For each 16x16 tile, its four 8x8 screen entries (TL, TR, BL, BR) as (variant << 16) | unique 8x8 tile index.
    std::vector<uint32_t> tile16Entries;
    uint32_t tile16Count = 0;
    // The scrolling background's 20x20 tile pattern (160x160 px), as (variant << 16) | unique 8x8 tile index; empty if none.
    std::vector<uint32_t> scrollPattern;
    std::vector<TileAnimOut> anims;
    std::vector<RGBATile> animFrameTiles;
};

class MapCompiler final
{
public:
    MapCompiler() 
     : mTileSet16(16, 16)
    {

    }

    // bg2CSVPath / fgCSVPath may be missing files (the layer is then empty).
    MapCompileResult Load(const std::string& mapName, const std::string& mapBGCSVPath, const std::string& bg2CSVPath,
                          const std::string& fgCSVPath, const std::string& mapTilesetPath,
                          const std::string& scrollTexturePath = std::string(),
                          const std::vector<TileAnimSpec>& anims = {});

private:
    void SaveMapAsPNG(const std::string& pngName, Array2D<uint32_t>& map, TileSet& tileSet);
    void Build8x8Map(const Array2D<uint32_t>& map16,
                              const TileSplitResult& split,
                              Array2D<uint32_t>& outMap8);
    void Apply8x8Remap(Array2D<uint32_t>& map8,
                                const std::vector<DedupEntry>& remap8toUnique);

    void SaveTileSetAsPNG(const std::string& pngName, const TileSet& tileSet);
    void CompileTileAnims(const std::vector<TileAnimSpec>& anims, TileDedupResult& dedup, MapCompileResult& ret);
    bool CompileTileAnims(const std::vector<TileAnimSpec>& anims, TileDedupResult& dedup, MapCompileResult& ret,
                          bool onlyUsed, uint32_t maxTiles);

    Array2D<uint32_t> mMap16;
    TileSet mTileSet16;
};
