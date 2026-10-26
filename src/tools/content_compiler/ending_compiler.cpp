#include "ending_compiler.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "lodepng.h"

namespace
{
constexpr int kFirstColour = 16;

uint16_t To555(const uint8_t* p)
{
    auto q = [](unsigned v) { return v >= 252 ? 31u : (v + 4) >> 3; };
    return static_cast<uint16_t>(q(p[0]) | (q(p[1]) << 5) | (q(p[2]) << 10));
}
} // namespace

bool CompileEnding(const ContentPaths& paths)
{
    const std::string path = paths.FindTexturePath("go");
    std::vector<uint8_t> rgba;
    unsigned w = 0;
    unsigned h = 0;
    if (path.empty() || lodepng::decode(rgba, w, h, path, LCT_RGBA, 8) || w % 8 != 0 || h % 8 != 0)
    {
        printf("ERROR: ending texture go not found/readable\n");
        return false;
    }

    std::vector<uint16_t> palette;
    std::map<uint16_t, uint8_t> lookup;
    std::vector<uint8_t> indices(w * h, 0);
    for (unsigned i = 0; i < w * h; i++)
    {
        const uint8_t* p = &rgba[i * 4];
        if (p[3] < 128)
        {
            continue;
        }
        const uint16_t c = To555(p);
        if (!lookup.count(c))
        {
            if (kFirstColour + palette.size() > 255)
            {
                printf("ERROR: ending palette doesn't fit\n");
                return false;
            }
            lookup[c] = static_cast<uint8_t>(kFirstColour + palette.size());
            palette.push_back(c);
        }
        indices[i] = lookup[c];
    }

    std::vector<std::vector<uint8_t>> tiles(1, std::vector<uint8_t>(64, 0)); // tile 0 = empty
    std::map<std::vector<uint8_t>, uint16_t> tileIndex;
    tileIndex[tiles[0]] = 0;
    std::vector<uint16_t> map;
    for (unsigned ty = 0; ty < h / 8; ty++)
    {
        for (unsigned tx = 0; tx < w / 8; tx++)
        {
            std::vector<uint8_t> t(64);
            for (int y = 0; y < 8; y++)
            {
                for (int x = 0; x < 8; x++)
                {
                    t[y * 8 + x] = indices[(ty * 8 + y) * w + tx * 8 + x];
                }
            }
            uint16_t entry = 0;
            bool found = false;
            for (int flip = 0; flip < 4 && !found; flip++)
            {
                std::vector<uint8_t> v(64);
                for (int y = 0; y < 8; y++)
                {
                    for (int x = 0; x < 8; x++)
                    {
                        v[y * 8 + x] = t[((flip & 2) ? 7 - y : y) * 8 + ((flip & 1) ? 7 - x : x)];
                    }
                }
                auto it = tileIndex.find(v);
                if (it != tileIndex.end())
                {
                    entry = static_cast<uint16_t>(it->second | ((flip & 1) ? 0x400 : 0) | ((flip & 2) ? 0x800 : 0));
                    found = true;
                }
            }
            if (!found)
            {
                entry = static_cast<uint16_t>(tiles.size());
                tileIndex[t] = entry;
                tiles.push_back(t);
            }
            map.push_back(entry);
        }
    }

    std::string out = "// Auto-generated credits data (see ending_compiler.hpp)\nnamespace ending\n{\n";
    char buf[256];
    snprintf(buf, sizeof(buf), "const unsigned short kGoTilesW = %u;\nconst unsigned short kGoTilesH = %u;\n", w / 8, h / 8);
    out += buf;
    snprintf(buf, sizeof(buf), "const unsigned short kGoFirstColour = %d;\nconst unsigned short kGoColours = %zu;\n",
             kFirstColour, palette.size());
    out += buf;
    out += "const unsigned short kGoPalette[] = {";
    for (size_t i = 0; i < palette.size(); i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 16 == 0) ? "\n    " : "", palette[i]);
        out += buf;
    }
    out += "\n};\n";
    snprintf(buf, sizeof(buf), "const unsigned short kGoMap[%zu] = {", map.size());
    out += buf;
    for (size_t i = 0; i < map.size(); i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 20 == 0) ? "\n    " : "", map[i]);
        out += buf;
    }
    out += "\n};\n";
    snprintf(buf, sizeof(buf), "const unsigned short kGoTileCount = %zu;\nalignas(4) const unsigned char kGoTiles[%zu] = {",
             tiles.size(), tiles.size() * 64);
    out += buf;
    size_t n = 0;
    for (const auto& t : tiles)
    {
        for (uint8_t b : t)
        {
            snprintf(buf, sizeof(buf), "%s%u,", (n % 32 == 0) ? "\n    " : "", b);
            out += buf;
            n++;
        }
    }
    out += "\n};\n}\n";
    printf("Ending: %zu unique tiles, %zu colours\n", tiles.size(), palette.size());
    WriteFileIfChanged("ending_data.h", out);
    return true;
}
