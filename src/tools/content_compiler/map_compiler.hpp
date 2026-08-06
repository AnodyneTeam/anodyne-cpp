#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <array>

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

struct TileSplitResult;

class TileSet final
{
public:
    TileSet(uint32_t tileWidth, uint32_t tileHeight);
    bool Load(const std::string& pngPath);
    const Array2D<RGBATile>& GetTiles() const
    {
        return mTiles;
    }

    TileSplitResult Make8x8WithRemap() const;
    uint32_t GetTileWidth() const { return mTileWidth; }
    uint32_t GetTileHeight() const { return mTileHeight; }
private:
    uint32_t mTileWidth = 0;
    uint32_t mTileHeight = 0;
    Array2D<RGBATile> mTiles;
};

struct TileSplitResult
{
    TileSplitResult()
     : tiles8x8(8, 8)
    {

    }

    TileSet tiles8x8;
    std::vector<std::array<uint32_t,4>> remap16to8;
};

class MapCompiler final
{
public:
    MapCompiler() 
     : mTileSet16(16, 16)
    {

    }

    bool Load(const std::string& mapBGCSVPath, const std::string& mapTilesetPath);
    void Compile(const std::string& mapName);
private:
    void SaveMapAsPNG(const std::string& pngName, Array2D<uint32_t>& map, TileSet& tileSet);
    void Build8x8Map(const Array2D<uint32_t>& map16,
                              const TileSplitResult& split,
                              Array2D<uint32_t>& outMap8);

    Array2D<uint32_t> mMap16;
    TileSet mTileSet16;
};
