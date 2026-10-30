#include "hud_compiler.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "lodepng.h"

namespace
{
uint16_t To555(uint8_t r, uint8_t g, uint8_t b)
{
    auto q = [](unsigned v) { return v >= 252 ? 31u : (v + 4) >> 3; };
    return static_cast<uint16_t>(q(r) | (q(g) << 5) | (q(b) << 10));
}

struct Rgba
{
    std::vector<uint8_t> mPixels;
    unsigned mW = 0;
    unsigned mH = 0;
};

bool Load(const ContentPaths& paths, const char* stem, Rgba& out)
{
    const std::string path = paths.FindTexturePath(stem);
    if (path.empty() || lodepng::decode(out.mPixels, out.mW, out.mH, path, LCT_RGBA, 8))
    {
        printf("ERROR: HUD texture %s not found/readable\n", stem);
        return false;
    }
    return true;
}

// A bank's colours from index `first` on.
struct Bank
{
    int mFirst;
    int mLast;
    std::vector<uint16_t> mColours;

    int Index(uint16_t c)
    {
        for (size_t i = 0; i < mColours.size(); i++)
        {
            if (mColours[i] == c)
            {
                return mFirst + static_cast<int>(i);
            }
        }
        if (mFirst + static_cast<int>(mColours.size()) > mLast)
        {
            return -1;
        }
        mColours.push_back(c);
        return mFirst + static_cast<int>(mColours.size()) - 1;
    }
};

std::string Hex(const std::vector<uint8_t>& bytes)
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

// The image's pixels as indices into the bank (0 transparent), cropped to (x, y, w, h).
bool Indices(const Rgba& img, Bank& bank, unsigned x0, unsigned y0, unsigned w, unsigned h, std::vector<uint8_t>& out)
{
    for (unsigned y = y0; y < y0 + h; y++)
    {
        for (unsigned x = x0; x < x0 + w; x++)
        {
            const uint8_t* p = &img.mPixels[(y * img.mW + x) * 4];
            if (p[3] < 128)
            {
                out.push_back(0);
                continue;
            }
            const int i = bank.Index(To555(p[0], p[1], p[2]));
            if (i < 0)
            {
                return false;
            }
            out.push_back(static_cast<uint8_t>(i));
        }
    }
    return true;
}

std::string Image(const char* id, unsigned w, unsigned h, unsigned frameW, unsigned frameH, const std::vector<uint8_t>& px)
{
    char buf[256];
    std::string out;
    snprintf(buf, sizeof(buf), "const unsigned char kPixels%s[%zu] = {", id, px.size());
    out += buf;
    out += Hex(px);
    snprintf(buf, sizeof(buf), "\n};\nconst Image kImg%s = { %u, %u, %u, %u, kPixels%s };\n", id, w, h, frameW, frameH, id);
    out += buf;
    return out;
}

std::string Palette(const char* id, const std::vector<uint16_t>& colours)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "const unsigned short %s[%zu] = {", id, colours.size());
    std::string out = buf;
    for (uint16_t c : colours)
    {
        snprintf(buf, sizeof(buf), "%u,", c);
        out += buf;
    }
    return out + "};\n";
}
} // namespace

bool CompileHud(const ContentPaths& paths)
{
    std::string out = "// Auto-generated HUD data (see hud_compiler.hpp)\nnamespace hud\n{\n";
    out += "struct Image\n{\n    unsigned short mW, mH, mFrameW, mFrameH;\n    const unsigned char* mPixels;\n};\n";

    // Bank 14: black first (the panel behind the HUD), then the label's colour, then the art's.
    Bank bank14 = { 1, 15, {} };
    const int black = bank14.Index(To555(0, 0, 0));
    const int label = bank14.Index(To555(124, 163, 177));
    char buf[256];
    snprintf(buf, sizeof(buf), "const unsigned char kColourBlack = %d;\nconst unsigned char kColourLabel = %d;\n", black, label);
    out += buf;
    struct Spec
    {
        const char* mId;
        const char* mStem;
        unsigned mX, mY, mW, mH; // crop (w 0: the whole texture)
        unsigned mFrameW, mFrameH;
    };
    const Spec specs14[] = {
        { "Frame", "frame_icon", 0, 0, 0, 0, 14, 14 },
        { "Dust", "DustUI", 0, 0, 0, 0, 9, 9 },
        { "Key", "header", 30, 3, 6, 14, 6, 14 }, // (the key drawn on the C#'s header strip)
        { "Health", "health_piece", 0, 0, 0, 0, 11, 6 },
    };
    for (const Spec& s : specs14)
    {
        Rgba img;
        if (!Load(paths, s.mStem, img))
        {
            return false;
        }
        const unsigned w = s.mW ? s.mW : img.mW;
        const unsigned h = s.mW ? s.mH : img.mH;
        std::vector<uint8_t> px;
        if (!Indices(img, bank14, s.mX, s.mY, w, h, px))
        {
            printf("ERROR: the HUD's palette bank 14 is full at %s\n", s.mStem);
            return false;
        }
        out += Image(s.mId, w, h, s.mFrameW, s.mFrameH, px);
    }
    out += Palette("kPalette14", bank14.mColours);

    // Bank 13: the mini minimap's colours, then each broom icon's own from index 5.
    Bank minimap = { 1, 4, {} };
    {
        Rgba img;
        if (!Load(paths, "mini_minimap_tiles", img))
        {
            return false;
        }
        std::vector<uint8_t> px;
        if (!Indices(img, minimap, 0, 0, img.mW, img.mH, px))
        {
            printf("ERROR: the mini minimap has more than 4 colours\n");
            return false;
        }
        out += Image("MiniMinimap", img.mW, img.mH, 5, 5, px);
    }
    out += Palette("kPaletteMinimap", minimap.mColours);

    // PlayState.UpdateBroomIcon's textures, in HudIcon order. More colours than fit (the knife) go to the nearest of
    // the most used ones.
    const char* const icons[] = { "hudNormal", "hudCell", "hudKnife", "hudWide", "hudLong", "hudTransform" };
    std::string table = "const Image* const kIcons[] = {";
    std::string palettes = "const unsigned short* const kIconPalettes[] = {";
    for (const char* stem : icons)
    {
        Rgba img;
        if (!Load(paths, stem, img))
        {
            return false;
        }
        std::map<uint16_t, int> uses;
        for (unsigned i = 0; i < img.mW * img.mH; i++)
        {
            const uint8_t* p = &img.mPixels[i * 4];
            if (p[3] >= 128)
            {
                uses[To555(p[0], p[1], p[2])]++;
            }
        }
        std::vector<std::pair<int, uint16_t>> byUse;
        for (const auto& [c, n] : uses)
        {
            byUse.push_back({ -n, c });
        }
        std::sort(byUse.begin(), byUse.end());
        std::vector<uint16_t> keep;
        for (size_t i = 0; i < byUse.size() && i < 11; i++)
        {
            keep.push_back(byUse[i].second);
        }
        auto nearest = [&](uint16_t c) {
            int best = 0;
            int bestD = 1 << 30;
            for (size_t i = 0; i < keep.size(); i++)
            {
                const int dr = (c & 31) - (keep[i] & 31);
                const int dg = ((c >> 5) & 31) - ((keep[i] >> 5) & 31);
                const int db = ((c >> 10) & 31) - ((keep[i] >> 10) & 31);
                const int d = dr * dr + dg * dg + db * db;
                if (d < bestD)
                {
                    bestD = d;
                    best = static_cast<int>(i);
                }
            }
            return best;
        };
        std::vector<uint8_t> px;
        for (unsigned i = 0; i < img.mW * img.mH; i++)
        {
            const uint8_t* p = &img.mPixels[i * 4];
            px.push_back(p[3] < 128 ? 0 : static_cast<uint8_t>(5 + nearest(To555(p[0], p[1], p[2]))));
        }
        const std::string id = std::string("Icon_") + stem;
        out += Image(id.c_str(), img.mW, img.mH, img.mW, img.mH, px);
        keep.resize(11, 0);
        out += Palette(("kPalette" + id).c_str(), keep);
        table += " &kImg" + id + ",";
        palettes += " kPalette" + id + ",";
    }
    out += table + " };\n" + palettes + " };\n}\n";
    printf("HUD: bank 14 %zu colours, %zu broom icons\n", bank14.mColours.size(), sizeof(icons) / sizeof(icons[0]));
    WriteFileIfChanged("hud_data.h", out);
    return true;
}
