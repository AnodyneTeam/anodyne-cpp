#include "title_compiler.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "lodepng.h"
#include "lz77.hpp"

namespace
{
constexpr int kMaxImageColours = 175; // + transparent index 0 (banks 13-15 hold the red prompt and the text colours)

struct Image final
{
    std::string mName;
    unsigned mW = 0;
    unsigned mH = 0;
    std::vector<uint8_t> mRgba;
    std::vector<uint8_t> mIndices; // palette index per pixel (padded to whole tiles)
    unsigned mTilesW = 0;
    unsigned mTilesH = 0;
};

uint16_t To555(const uint8_t* p)
{
    auto q = [](unsigned v) { return v >= 252 ? 31u : (v + 4) >> 3; }; // round, don't truncate: the title's colours are tiny
    return static_cast<uint16_t>(q(p[0]) | (q(p[1]) << 5) | (q(p[2]) << 10));
}

bool LoadImage(const ContentPaths& paths, const std::string& stem, Image& out)
{
    const std::string path = paths.FindTexturePath(stem);
    if (path.empty() || lodepng::decode(out.mRgba, out.mW, out.mH, path, LCT_RGBA, 8))
    {
        printf("ERROR: title texture %s not found/readable\n", stem.c_str());
        return false;
    }
    out.mName = stem;
    out.mTilesW = (out.mW + 7) / 8;
    out.mTilesH = (out.mH + 7) / 8;
    return true;
}

std::string Hex(const std::vector<uint8_t>& data)
{
    std::string s;
    char buf[16];
    for (size_t i = 0; i < data.size(); i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 32 == 0) ? "\n    " : "", data[i]);
        s += buf;
    }
    return s;
}
} // namespace

bool CompileTitle(const ContentPaths& paths)
{
    // images[2] is the door with the glow disc (door_glow, at (48, 17)) baked in: the title's spinning glow sprites
    // blend additively with the BG underneath, and an OBJ can't see another OBJ below it, so the disc has to be BG.
    Image images[3];
    const char* const names[3] = { "title_bg", "door", "door" };
    for (int i = 0; i < 3; i++)
    {
        if (!LoadImage(paths, names[i], images[i]))
        {
            return false;
        }
    }
    // title_bg is (0,0,0) / (5,7,18): near-black in the C# too. Double it so the scrolling pattern is visible at all
    // on the GBA's coarser 5-bit colours and LCD.
    for (size_t i = 0; i + 3 < images[0].mRgba.size(); i += 4)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            const unsigned v = images[0].mRgba[i + ch] * 2;
            images[0].mRgba[i + ch] = static_cast<uint8_t>(v > 255 ? 255 : v);
        }
    }
    images[2].mName = "doorglow";
    {
        Image glow;
        if (!LoadImage(paths, "door_glow", glow))
        {
            return false;
        }
        for (unsigned y = 0; y < glow.mH; y++)
        {
            for (unsigned x = 0; x < glow.mW; x++)
            {
                const uint8_t* src = &glow.mRgba[(y * glow.mW + x) * 4];
                if (src[3] >= 128 && 48 + x < images[2].mW && 17 + y < images[2].mH)
                {
                    uint8_t* dst = &images[2].mRgba[((17 + y) * images[2].mW + 48 + x) * 4];
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 255;
                }
            }
        }
    }

    // ---- shared palette (index 0 = transparent) ----
    // Drop low bits of each channel until the distinct colours fit.
    std::vector<uint16_t> palette;
    std::map<uint16_t, uint8_t> lookup;
    for (int dropBits = 0; dropBits < 4; dropBits++)
    {
        palette.assign(1, 0);
        lookup.clear();
        const uint16_t mask = static_cast<uint16_t>((((0x1F >> dropBits) << dropBits) * 0x421) & 0x7FFF);
        bool overflow = false;
        for (const Image& img : images)
        {
            for (size_t i = 0; i < img.mRgba.size(); i += 4)
            {
                if (img.mRgba[i + 3] < 128)
                {
                    continue;
                }
                const uint16_t c = To555(&img.mRgba[i]) & mask;
                if (!lookup.count(c))
                {
                    if (palette.size() > kMaxImageColours)
                    {
                        overflow = true;
                        break;
                    }
                    lookup[c] = static_cast<uint8_t>(palette.size());
                    palette.push_back(c);
                }
            }
            if (overflow)
            {
                break;
            }
        }
        if (!overflow)
        {
            if (dropBits)
            {
                printf("Title: palette needed %d dropped bits per channel\n", dropBits);
            }
            palette.resize(256, 0);
            // lookup uses `mask`, remember it for pixels below
            for (Image& img : images)
            {
                img.mIndices.assign(img.mTilesW * 8 * img.mTilesH * 8, 0);
                for (unsigned y = 0; y < img.mH; y++)
                {
                    for (unsigned x = 0; x < img.mW; x++)
                    {
                        const uint8_t* p = &img.mRgba[(y * img.mW + x) * 4];
                        if (p[3] >= 128)
                        {
                            img.mIndices[y * img.mTilesW * 8 + x] = lookup[To555(p) & mask];
                        }
                    }
                }
            }
            break;
        }
        if (dropBits == 3)
        {
            printf("ERROR: title palette doesn't fit\n");
            return false;
        }
    }

    // ---- tiles (de-duplicated with flips) and maps ----
    std::vector<std::vector<uint8_t>> tiles(1, std::vector<uint8_t>(64, 0)); // tile 0 = empty
    std::map<std::vector<uint8_t>, uint16_t> tileIndex;
    tileIndex[tiles[0]] = 0;
    std::string maps;
    std::string out = "// Auto-generated title screen data (see title_compiler.hpp)\nnamespace title\n{\n";
    char buf[256];

    for (const Image& img : images)
    {
        const char* prefix = img.mName == "door" ? "Door" : (img.mName == "doorglow" ? "DoorGlow" : "Bg");
        std::vector<uint16_t> map;
        for (unsigned ty = 0; ty < img.mTilesH; ty++)
        {
            for (unsigned tx = 0; tx < img.mTilesW; tx++)
            {
                std::vector<uint8_t> t(64);
                for (int y = 0; y < 8; y++)
                {
                    for (int x = 0; x < 8; x++)
                    {
                        t[y * 8 + x] = img.mIndices[(ty * 8 + y) * img.mTilesW * 8 + tx * 8 + x];
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
        snprintf(buf, sizeof(buf), "const unsigned short k%sTilesW = %u;\nconst unsigned short k%sTilesH = %u;\n",
                 prefix, img.mTilesW, prefix, img.mTilesH);
        out += buf;
        snprintf(buf, sizeof(buf), "const unsigned short k%sMap[%zu] = {", prefix, map.size());
        out += buf;
        for (size_t i = 0; i < map.size(); i++)
        {
            snprintf(buf, sizeof(buf), "%s%u,", (i % 24 == 0) ? "\n    " : "", map[i]);
            out += buf;
        }
        out += "\n};\n";
    }

    if (tiles.size() > 512)
    {
        printf("ERROR: title needs %zu tiles (max 512)\n", tiles.size());
        return false;
    }
    std::vector<uint8_t> tileBytes;
    for (const auto& t : tiles)
    {
        tileBytes.insert(tileBytes.end(), t.begin(), t.end());
    }
    const std::vector<uint8_t> packed = CompressLz77(tileBytes.data(), tileBytes.size());
    snprintf(buf, sizeof(buf), "const unsigned int kTilesUnpackedSize = %zu;\nalignas(4) const unsigned char kTilesPacked[%zu] = {",
             tileBytes.size(), packed.size());
    out += buf;
    out += Hex(packed) + "\n};\n";

    out += "const unsigned short kPalette[256] = {";
    for (size_t i = 0; i < 256; i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 16 == 0) ? "\n    " : "", palette[i]);
        out += buf;
    }
    out += "\n};\n";
    printf("Title: %zu unique tiles, %zu colours, tiles %zu -> %zu bytes\n", tiles.size(), lookup.size() + 1, tileBytes.size(),
           packed.size());

    // ---- font: 7x8 glyphs in 8x8 4bpp tiles, ink = index 1 ----
    Image font;
    if (!LoadImage(paths, "font-white-apple-7x8", font))
    {
        return false;
    }
    const std::string order = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ'1234567890.:,;'\"(!?)+-*/=$[]_";
    std::vector<uint8_t> glyphs; // 32 bytes per glyph
    uint8_t asciiToTile[128] = {};
    for (size_t g = 0; g < order.size(); g++)
    {
        const unsigned cellX = static_cast<unsigned>(g % 27) * 7;
        const unsigned cellY = static_cast<unsigned>(g / 27) * 8;
        uint8_t px[8][8] = {};
        for (unsigned y = 0; y < 8; y++)
        {
            for (unsigned x = 0; x < 7; x++)
            {
                if (cellX + x < font.mW && cellY + y < font.mH && font.mRgba[((cellY + y) * font.mW + cellX + x) * 4 + 3] >= 128)
                {
                    px[y][x] = 1;
                }
            }
        }
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 8; x += 2)
            {
                glyphs.push_back(static_cast<uint8_t>(px[y][x] | (px[y][x + 1] << 4)));
            }
        }
        if (asciiToTile[static_cast<uint8_t>(order[g])] == 0 || order[g] != '\'')
        {
            asciiToTile[static_cast<uint8_t>(order[g])] = static_cast<uint8_t>(g);
        }
    }
    snprintf(buf, sizeof(buf), "alignas(4) const unsigned char kFontTiles[%zu] = {", glyphs.size());
    out += buf;
    out += Hex(glyphs) + "\n};\n";
    snprintf(buf, sizeof(buf), "const unsigned short kFontTileCount = %zu;\n", order.size());
    out += buf;
    out += "const unsigned char kFontAscii[128] = {";
    for (int i = 0; i < 128; i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % 32 == 0) ? "\n    " : "", asciiToTile[i]);
        out += buf;
    }
    out += "\n};\n}\n";

    // ---- title credits: lines 2-7 of `npc misc / area any / scene title` ----
    {
        std::ifstream dlg(paths.GetDialogPath("EN"));
        std::string line;
        bool inMisc = false;
        bool inAny = false;
        bool inScene = false;
        std::vector<std::string> lines;
        while (std::getline(dlg, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            {
                line.pop_back();
            }
            const size_t b = line.find_first_not_of(" \t");
            const std::string t = b == std::string::npos ? std::string() : line.substr(b);
            if (t.rfind("npc ", 0) == 0)
            {
                inMisc = t == "npc misc";
                inAny = inScene = false;
            }
            else if (inMisc && t.rfind("area ", 0) == 0)
            {
                inAny = t == "area any";
                inScene = false;
            }
            else if (inAny && t == "scene title")
            {
                inScene = true;
            }
            else if (inScene && t.rfind("end scene", 0) == 0)
            {
                break;
            }
            else if (inScene && !t.empty() && t[0] != '#')
            {
                lines.push_back(t);
            }
        }
        out.resize(out.size() - 2);
        out += "const char* const kCreditLines[6] = {\n";
        for (int i = 2; i <= 7 && i < static_cast<int>(lines.size()); i++)
        {
            out += "    \"" + lines[i] + "\",\n";
        }
        out += "};\n}\n";
    }

    // ---- the new-game intro dialogue (npc sage / area BLANK / scene intro) ----
    {
        std::ifstream dlg(paths.GetDialogPath("EN"));
        std::string line;
        bool inSage = false;
        bool inBlank = false;
        bool inScene = false;
        std::vector<std::string> pages;
        auto trim = [](std::string t)
        {
            const char* ws = " \t\r\n";
            const size_t b = t.find_first_not_of(ws);
            return b == std::string::npos ? std::string() : t.substr(b, t.find_last_not_of(ws) - b + 1);
        };
        auto replaceAll = [](std::string& t, const std::string& from, const std::string& to)
        {
            for (size_t p = t.find(from); p != std::string::npos; p = t.find(from, p + to.size()))
            {
                t.replace(p, from.size(), to);
            }
        };
        while (std::getline(dlg, line))
        {
            const std::string t = trim(line);
            if (t.rfind("npc ", 0) == 0)
            {
                inSage = t == "npc sage";
                inBlank = inScene = false;
            }
            else if (inSage && t.rfind("area ", 0) == 0)
            {
                inBlank = t == "area BLANK";
                inScene = false;
            }
            else if (inBlank && t == "scene intro")
            {
                inScene = true;
            }
            else if (inScene && t.rfind("end scene", 0) == 0)
            {
                break;
            }
            else if (inScene && !t.empty() && t[0] != '#')
            {
                std::string text = t;
                replaceAll(text, "[SOMEKEY-C]", "A");
                replaceAll(text, "[SOMEKEY-ENTER]", "START");
                replaceAll(text, "\xE2\x80\x99", "'");
                replaceAll(text, "\\n", "\n");
                std::string ascii;
                for (char ch : text)
                {
                    if (static_cast<unsigned char>(ch) < 128)
                    {
                        ascii += ch;
                    }
                }
                // '^' = wait for a button press and start a fresh box.
                size_t start = 0;
                for (size_t i = 0; i <= ascii.size(); i++)
                {
                    if (i == ascii.size() || ascii[i] == '^')
                    {
                        const std::string page = trim(ascii.substr(start, i - start));
                        if (!page.empty())
                        {
                            pages.push_back(page);
                        }
                        start = i + 1;
                    }
                }
            }
        }
        out.resize(out.size() - 2); // reopen the namespace
        out += "const unsigned short kIntroPageCount = " + std::to_string(pages.size()) + ";\nconst char* const kIntroPages[] = {\n";
        for (const std::string& page : pages)
        {
            out += "    \"";
            for (char ch : page)
            {
                if (ch == '\n')
                {
                    out += "\\n";
                }
                else if (ch == '"' || ch == '\\')
                {
                    out += std::string("\\") + ch;
                }
                else
                {
                    out += ch;
                }
            }
            out += "\",\n";
        }
        out += "};\n}\n";
        printf("Title: %zu intro pages\n", pages.size());
    }

    WriteFileIfChanged("title_data.h", out);
    return true;
}
