#include "ending_compiler.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "lodepng.h"
#include "lz77.hpp"

namespace
{
constexpr int kFirstColour = 16;

uint16_t To555(const uint8_t* p)
{
    auto q = [](unsigned v) { return v >= 252 ? 31u : (v + 4) >> 3; };
    return static_cast<uint16_t>(q(p[0]) | (q(p[1]) << 5) | (q(p[2]) << 10));
}
// Cuts the (w-wide) index image's tilesW x tilesH tiles at (x0, y0) into `tiles`, de-duplicated with flips (the ones
// already there count), and appends their screen entries to `map`.
void AddTiles(const std::vector<uint8_t>& indices, unsigned w, unsigned x0, unsigned y0, unsigned tilesW, unsigned tilesH,
              std::vector<std::vector<uint8_t>>& tiles, std::vector<uint16_t>& map)
{
    std::map<std::vector<uint8_t>, uint16_t> tileIndex;
    for (size_t i = 0; i < tiles.size(); i++)
    {
        tileIndex.emplace(tiles[i], static_cast<uint16_t>(i));
    }
    for (unsigned ty = 0; ty < tilesH; ty++)
    {
        for (unsigned tx = 0; tx < tilesW; tx++)
        {
            std::vector<uint8_t> t(64);
            for (int y = 0; y < 8; y++)
            {
                for (int x = 0; x < 8; x++)
                {
                    t[y * 8 + x] = indices[(y0 + ty * 8 + y) * w + x0 + tx * 8 + x];
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
}

// Median cut: at most `count` colours for `colours` (RGB555, with how often each is used); `lookup` maps each to its
// index in the result.
std::vector<uint16_t> Reduce(const std::map<uint16_t, int>& colours, size_t count, std::map<uint16_t, uint8_t>& lookup)
{
    struct Entry
    {
        uint16_t mColour;
        int mUses;
    };
    auto channel = [](uint16_t c, int ch) { return (c >> (ch * 5)) & 31; };
    std::vector<std::vector<Entry>> boxes(1);
    for (const auto& [c, n] : colours)
    {
        boxes[0].push_back({ c, n });
    }
    while (boxes.size() < count)
    {
        // Split the box with the widest channel range (weighted by its pixels) at its median pixel.
        int best = -1;
        int bestCh = 0;
        long bestScore = 0;
        for (size_t b = 0; b < boxes.size(); b++)
        {
            if (boxes[b].size() < 2)
            {
                continue;
            }
            long pixels = 0;
            for (const Entry& e : boxes[b])
            {
                pixels += e.mUses;
            }
            for (int ch = 0; ch < 3; ch++)
            {
                int lo = 31;
                int hi = 0;
                for (const Entry& e : boxes[b])
                {
                    lo = std::min(lo, channel(e.mColour, ch));
                    hi = std::max(hi, channel(e.mColour, ch));
                }
                const long score = static_cast<long>(hi - lo) * (hi - lo) * pixels;
                if (best < 0 || score > bestScore)
                {
                    best = static_cast<int>(b);
                    bestCh = ch;
                    bestScore = score;
                }
            }
        }
        if (best < 0 || bestScore == 0)
        {
            break;
        }
        std::vector<Entry>& box = boxes[best];
        std::sort(box.begin(), box.end(), [&](const Entry& a, const Entry& b) {
            return channel(a.mColour, bestCh) < channel(b.mColour, bestCh);
        });
        long total = 0;
        for (const Entry& e : box)
        {
            total += e.mUses;
        }
        long run = 0;
        size_t cut = 1;
        for (; cut < box.size() - 1; cut++)
        {
            run += box[cut - 1].mUses;
            if (run * 2 >= total)
            {
                break;
            }
        }
        std::vector<Entry> upper(box.begin() + static_cast<long>(cut), box.end());
        box.resize(cut);
        boxes.push_back(std::move(upper));
    }
    std::vector<uint16_t> palette;
    for (const std::vector<Entry>& box : boxes)
    {
        long sum[3] = { 0, 0, 0 };
        long n = 0;
        for (const Entry& e : box)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                sum[ch] += static_cast<long>(channel(e.mColour, ch)) * e.mUses;
            }
            n += e.mUses;
        }
        uint16_t c = 0;
        for (int ch = 0; ch < 3; ch++)
        {
            c = static_cast<uint16_t>(c | (((sum[ch] + n / 2) / n) << (ch * 5)));
        }
        for (const Entry& e : box)
        {
            lookup[e.mColour] = static_cast<uint8_t>(palette.size());
        }
        palette.push_back(c);
    }
    return palette;
}

std::string Bytes(const std::vector<uint8_t>& bytes)
{
    std::string out;
    char buf[16];
    for (size_t i = 0; i < bytes.size(); i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 32 == 0) ? "\n    " : "", bytes[i]);
        out += buf;
    }
    return out;
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
    std::vector<uint16_t> map;
    AddTiles(indices, w, 0, 0, w / 8, h / 8, tiles, map);

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
    out += "\n};\n";

    // The screenies (CreditsState.Screenie): 160x160 frames, each its own 8bpp tiles and map, its colours cut down to
    // kScreenieColours so two fit the palette at once (after go's: the first slot's at kGoFirstColour + kGoColours,
    // the second's kScreenieColours on). Tile pixels are 0-based: the game adds its slot's first colour.
    const std::string screeniePath = paths.FindTexturePath("screenies");
    std::vector<uint8_t> shots;
    unsigned sw = 0;
    unsigned sh = 0;
    if (screeniePath.empty() || lodepng::decode(shots, sw, sh, screeniePath, LCT_RGBA, 8) || sw % 160 != 0 || sh % 160 != 0)
    {
        printf("ERROR: ending texture screenies not found/readable\n");
        return false;
    }
    const size_t screenieColours = (256 - kFirstColour - palette.size()) / 2;
    const unsigned perRow = sw / 160;
    const unsigned count = perRow * (sh / 160);
    snprintf(buf, sizeof(buf), "const unsigned short kScreenieColours = %zu;\nconst unsigned short kScreenieCount = %u;\n",
             screenieColours, count);
    out += buf;
    std::string table = "const Screenie kScreenies[] = {\n";
    size_t packedBytes = 0;
    size_t maxTiles = 0;
    for (unsigned f = 0; f < count; f++)
    {
        const unsigned x0 = (f % perRow) * 160;
        const unsigned y0 = (f / perRow) * 160;
        std::map<uint16_t, int> uses;
        for (unsigned y = 0; y < 160; y++)
        {
            for (unsigned x = 0; x < 160; x++)
            {
                uses[To555(&shots[((y0 + y) * sw + x0 + x) * 4])]++;
            }
        }
        std::map<uint16_t, uint8_t> lookup;
        const std::vector<uint16_t> pal = Reduce(uses, screenieColours, lookup);
        std::vector<uint8_t> frame(160 * 160);
        for (unsigned y = 0; y < 160; y++)
        {
            for (unsigned x = 0; x < 160; x++)
            {
                frame[y * 160 + x] = lookup[To555(&shots[((y0 + y) * sw + x0 + x) * 4])];
            }
        }
        std::vector<std::vector<uint8_t>> shotTiles;
        std::vector<uint16_t> shotMap;
        AddTiles(frame, 160, 0, 0, 20, 20, shotTiles, shotMap);
        maxTiles = std::max(maxTiles, shotTiles.size());
        std::vector<uint8_t> raw;
        for (const auto& t : shotTiles)
        {
            raw.insert(raw.end(), t.begin(), t.end());
        }
        const std::vector<uint8_t> packed = CompressLz77(raw.data(), raw.size());
        packedBytes += packed.size();
        snprintf(buf, sizeof(buf), "alignas(4) const unsigned char kScreenieTiles%u[%zu] = {", f, packed.size());
        out += buf;
        out += Bytes(packed);
        snprintf(buf, sizeof(buf), "\n};\nconst unsigned short kScreenieMap%u[400] = {", f);
        out += buf;
        for (size_t i = 0; i < shotMap.size(); i++)
        {
            snprintf(buf, sizeof(buf), "%s%u,", (i % 20 == 0) ? "\n    " : "", shotMap[i]);
            out += buf;
        }
        snprintf(buf, sizeof(buf), "\n};\nconst unsigned short kScreeniePalette%u[%zu] = {", f, pal.size());
        out += buf;
        for (uint16_t c : pal)
        {
            snprintf(buf, sizeof(buf), "%u,", c);
            out += buf;
        }
        out += "};\n";
        snprintf(buf, sizeof(buf), "    { kScreenieTiles%u, %zu, kScreenieMap%u, kScreeniePalette%u, %zu },\n", f,
                 shotTiles.size(), f, f, pal.size());
        table += buf;
        if (uses.size() > screenieColours)
        {
            printf("Ending: screenie %u: %zu colours cut to %zu\n", f, uses.size(), pal.size());
        }
    }
    out += "struct Screenie\n{\n    const unsigned char* mTiles; // LZ77\n    unsigned short mTileCount;\n"
           "    const unsigned short* mMap;\n    const unsigned short* mPalette;\n    unsigned short mColours;\n};\n";
    out += table + "};\n}\n";
    printf("Ending: %zu unique tiles, %zu colours; %u screenies, %zu bytes LZ77, at most %zu tiles\n", tiles.size(),
           palette.size(), count, packedBytes, maxTiles);
    WriteFileIfChanged("ending_data.h", out);
    return true;
}
