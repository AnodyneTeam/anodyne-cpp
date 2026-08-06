#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdio.h>

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
};

class TileSet final
{
public:
    TileSet(uint32_t tileWidth, uint32_t tileHeight);
    bool Load(const std::string& pngPath);
    const Array2D<RGBATile>& GetTiles() const
    {
        return mTiles;
    }
private:
    uint32_t mTileWidth = 0;
    uint32_t mTileHeight = 0;
    Array2D<RGBATile> mTiles;
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
    void SaveOGMapAsPNG(const std::string& mapName);

    Array2D<uint32_t> mMap;
    TileSet mTileSet16;
};
