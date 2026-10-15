#include "collision_compiler.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

#include "file_utils.hpp"
#include "lz77.hpp"
#include "map_compiler.hpp"

namespace
{
// Same values as AnodyneSharp's Touching enum (GameObject.cs) and CollisionEventType (TileData.cs).
enum Touching : uint8_t
{
    TOUCH_NONE = 0,
    TOUCH_LEFT = 1,
    TOUCH_RIGHT = 2,
    TOUCH_UP = 4,
    TOUCH_DOWN = 8,
    TOUCH_ANY = 15
};

const char* const kEventNames[] = { "NONE",  "CONVEYOR", "THIN",       "HOLE",  "SLOW",
                                    "SPIKE", "LADDER",   "PUDDLE",     "REFLECTION", "GRASS" };

struct TileCollision final
{
    uint8_t mAllow = TOUCH_NONE;
    uint8_t mEvent = 0;
    uint8_t mDirection = TOUCH_ANY;

    bool operator<(const TileCollision& o) const
    {
        return std::tie(mAllow, mEvent, mDirection) < std::tie(o.mAllow, o.mEvent, o.mDirection);
    }
};

std::string Trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\r' || s[b] == '\n' || s[b] == '\t'))
    {
        b++;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == '\t'))
    {
        e--;
    }
    return s.substr(b, e - b);
}

// Enum.TryParse for the Touching flags enum: "ANY", "LEFT", or a comma separated combination.
bool ParseTouching(const std::string& text, uint8_t& out)
{
    out = TOUCH_NONE;
    std::stringstream ss(text);
    std::string part;
    bool any = false;
    while (std::getline(ss, part, ','))
    {
        part = Trim(part);
        any = true;
        if (part == "NONE") {}
        else if (part == "LEFT") { out |= TOUCH_LEFT; }
        else if (part == "RIGHT") { out |= TOUCH_RIGHT; }
        else if (part == "UP") { out |= TOUCH_UP; }
        else if (part == "DOWN") { out |= TOUCH_DOWN; }
        else if (part == "ANY") { out |= TOUCH_ANY; }
        else { return false; }
    }
    return any;
}

bool ParseEvent(const std::string& text, uint8_t& out)
{
    const std::string t = Trim(text);
    for (size_t i = 0; i < sizeof(kEventNames) / sizeof(kEventNames[0]); i++)
    {
        if (t == kEventNames[i])
        {
            out = static_cast<uint8_t>(i);
            return true;
        }
    }
    return false;
}

// Map.cs: tile 0 is invisible with no collision, every other tile defaults to colliding from all sides;
// TileData.col lines ("start[-end]<TAB>Touching[<TAB>Event[<TAB>Direction]]") then override ranges in order.
std::vector<TileCollision> LoadTileCollision(const std::string& colPath, uint32_t tileCount)
{
    std::vector<TileCollision> tiles(tileCount);
    for (uint32_t i = 1; i < tileCount; i++)
    {
        tiles[i].mAllow = TOUCH_ANY;
    }

    std::ifstream file(colPath);
    std::string line;
    bool first = true;
    while (std::getline(file, line))
    {
        if (first && line.size() >= 3 && static_cast<uint8_t>(line[0]) == 0xEF) // UTF-8 BOM
        {
            line.erase(0, 3);
        }
        first = false;

        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '\t'))
        {
            fields.push_back(field);
        }
        if (fields.size() < 2)
        {
            continue;
        }

        TileCollision value;
        uint8_t direction = TOUCH_ANY;
        uint8_t event = 0;
        if (!ParseTouching(fields[1], value.mAllow) ||
            (fields.size() > 2 && !ParseEvent(fields[2], event)) ||
            (fields.size() > 3 && !ParseTouching(fields[3], direction)))
        {
            continue; // the C# silently skips lines that don't parse
        }
        value.mEvent = event;
        value.mDirection = direction;

        int start = 0;
        int end = 0;
        const std::string range = Trim(fields[0]);
        const size_t dash = range.find('-');
        if (dash != std::string::npos)
        {
            start = std::atoi(range.substr(0, dash).c_str());
            end = std::atoi(range.substr(dash + 1).c_str());
        }
        else
        {
            start = end = std::atoi(range.c_str());
        }

        for (int i = start; i <= end; i++)
        {
            if (i >= 0 && static_cast<uint32_t>(i) < tileCount)
            {
                tiles[i] = value;
            }
        }
    }
    return tiles;
}

uint32_t LayerTile(const Array2D<uint32_t>& layer, bool present, uint32_t x, uint32_t y)
{
    // TileMap.GetTile: anything outside the layer is tile 0.
    if (!present || x >= layer.Width() || y >= layer.Height())
    {
        return 0;
    }
    return layer.Get(x, y);
}
} // namespace

bool CompileMapCollision(const ContentPaths& contentPaths, const std::string& mapName)
{
    CsvMap csv;
    Array2D<uint32_t> bg;
    if (!csv.Load(contentPaths.GetMapBGCSVPath(mapName), bg))
    {
        printf("Collision: can't read BG.csv for %s\n", mapName.c_str());
        return false;
    }

    Array2D<uint32_t> bg2;
    const bool haveBg2 = csv.Load(contentPaths.GetMapLayerCSVPath(mapName, "BG2"), bg2);

    uint32_t maxTile = 0;
    for (uint32_t y = 0; y < bg.Height(); y++)
    {
        for (uint32_t x = 0; x < bg.Width(); x++)
        {
            maxTile = std::max(maxTile, bg.Get(x, y));
        }
    }
    if (haveBg2)
    {
        for (uint32_t y = 0; y < bg2.Height(); y++)
        {
            for (uint32_t x = 0; x < bg2.Width(); x++)
            {
                maxTile = std::max(maxTile, bg2.Get(x, y));
            }
        }
    }

    const std::vector<TileCollision> tiles = LoadTileCollision(contentPaths.GetMapTileDataPath(mapName), maxTile + 1);

    // Merge BG + BG2 into one class per cell.
    std::map<TileCollision, uint8_t> classIndex;
    std::vector<TileCollision> classes;
    auto classFor = [&](const TileCollision& c) -> int
    {
        auto it = classIndex.find(c);
        if (it != classIndex.end())
        {
            return it->second;
        }
        if (classes.size() >= 256)
        {
            return -1;
        }
        const uint8_t index = static_cast<uint8_t>(classes.size());
        classIndex[c] = index;
        classes.push_back(c);
        return index;
    };
    classFor(TileCollision()); // class 0 = nothing

    const uint32_t width = bg.Width();
    const uint32_t height = bg.Height();
    std::vector<uint8_t> grid(static_cast<size_t>(width) * height, 0);
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            TileCollision merged = tiles[LayerTile(bg, true, x, y)];
            const uint32_t t2 = LayerTile(bg2, haveBg2, x, y);
            if (t2 != 0) // tile 0 never collides in BG2
            {
                const TileCollision& c2 = tiles[t2];
                merged.mAllow |= c2.mAllow;
                if (c2.mEvent != 0)
                {
                    merged.mEvent = c2.mEvent;
                    merged.mDirection = c2.mDirection;
                }
            }
            if (merged.mEvent == 0)
            {
                merged.mDirection = TOUCH_ANY; // direction only matters with an event; fewer classes
            }

            const int index = classFor(merged);
            if (index < 0)
            {
                printf("Collision: %s needs more than 256 collision classes\n", mapName.c_str());
                return false;
            }
            grid[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(index);
        }
    }

    const std::vector<uint8_t> packed = CompressLz77(grid.data(), grid.size());

    std::string out = "// Auto-generated map collision (see collision_compiler.hpp)\n";
    char buf[128];
    snprintf(buf, sizeof(buf), "namespace %s\n{\n", mapName.c_str());
    out += buf;
    snprintf(buf, sizeof(buf), "const unsigned short gColWidth = %u;\nconst unsigned short gColHeight = %u;\n", width, height);
    out += buf;
    snprintf(buf, sizeof(buf), "const unsigned short gColClassCount = %zu;\n", classes.size());
    out += buf;
    out += "const unsigned char gColClasses[] = {\n";
    for (const TileCollision& c : classes)
    {
        snprintf(buf, sizeof(buf), "    %u, %u, %u,\n", c.mAllow, c.mEvent, c.mDirection);
        out += buf;
    }
    out += "};\n";
    snprintf(buf, sizeof(buf), "const unsigned int gColUnpackedSize = %zu;\n", grid.size());
    out += buf;
    snprintf(buf, sizeof(buf), "alignas(4) const unsigned char gColPacked[%zu] = {", packed.size());
    out += buf;
    for (size_t i = 0; i < packed.size(); i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 32 == 0) ? "\n    " : "", packed[i]);
        out += buf;
    }
    out += "\n};\n}\n";

    WriteFileIfChanged((mapName + "_col.h").c_str(), out);
    printf("Collision %s: %ux%u cells, %zu classes, %zu -> %zu bytes\n", mapName.c_str(), width, height, classes.size(),
           grid.size(), packed.size());
    return true;
}
