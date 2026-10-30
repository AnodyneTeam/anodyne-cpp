#include "menu_compiler.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "lodepng.h"

namespace
{
// The palette groups: every page draws with the common colours plus its own group's.
enum Group
{
    GROUP_COMMON,
    GROUP_MAP,
    GROUP_EQUIP,
    GROUP_CARDS,
    GROUP_CONFIG,
    GROUP_SECRETS,
    GROUP_COUNT,
};

const char* const kGroupNames[GROUP_COUNT] = { "Common", "Map", "Equip", "Cards", "Config", "Secrets" };

struct ImageSpec
{
    const char* mId;      // the C++ name (kImg<Id>)
    const char* mTexture; // the PNG's stem
    int mFrameW;
    int mFrameH;
    Group mGroup;
    std::vector<int> mFrames; // only these frames, side by side (empty: the whole sheet)
};

// The text colours (UILabel): white with a black shadow, the "menu=enter" grey, the controls page's conflict red.
struct ColourSpec
{
    const char* mId;
    uint8_t mR, mG, mB;
};

const ColourSpec kColours[] = {
    { "White", 255, 255, 255 },
    { "Black", 0, 0, 0 },
    { "Grey", 143, 153, 176 },
    { "Red", 255, 0, 0 },
};

// menu_bg's colour behind the pages
const ColourSpec kPageBg = { "PageBg", 62, 69, 87 };

uint16_t To555(uint8_t r, uint8_t g, uint8_t b)
{
    auto q = [](unsigned v) { return v >= 252 ? 31u : (v + 4) >> 3; };
    return static_cast<uint16_t>(q(r) | (q(g) << 5) | (q(b) << 10));
}

struct Palette
{
    std::vector<uint16_t> mColours;
    std::map<uint16_t, uint8_t> mLookup;
};

std::string Hex(const std::vector<uint8_t>& bytes, int perLine)
{
    std::string out;
    char buf[16];
    for (size_t i = 0; i < bytes.size(); i++)
    {
        snprintf(buf, sizeof(buf), "%s%u,", (i % perLine == 0) ? "\n    " : "", bytes[i]);
        out += buf;
    }
    return out;
}
} // namespace

bool CompileMenu(const ContentPaths& paths)
{
    const std::vector<ImageSpec> images = {
        { "MenuBg", "menu_bg", 160, 160, GROUP_COMMON, {} },
        { "PageSelector", "menu_select_active", 61, 15, GROUP_COMMON, {} },
        { "Arrows", "arrows", 7, 7, GROUP_COMMON, {} },
        { "MinimapTiles", "minimap_tiles", 7, 7, GROUP_MAP, {} },
        { "NoneIcon", "none_icon", 18, 18, GROUP_EQUIP, {} },
        { "LongIcon", "long_icon", 18, 18, GROUP_EQUIP, {} },
        { "WideIcon", "wide_icon", 18, 18, GROUP_EQUIP, {} },
        { "TransformerIcon", "transformer_icon", 18, 18, GROUP_EQUIP, {} },
        { "EquippedIcon", "equipped_icon", 7, 10, GROUP_EQUIP, {} },
        { "JumpShoes", "item_jump_shoes", 16, 16, GROUP_EQUIP, {} },
        { "TradeItems", "fields_npcs", 16, 16, GROUP_EQUIP, { 56, 31 } }, // the shoes, the box
        { "BigKeys", "key_green", 16, 16, GROUP_EQUIP, {} },
        { "Cards", "card_sheet", 24, 24, GROUP_CARDS, {} },
        { "CheckBox", "checkbox", 11, 11, GROUP_CONFIG, {} },
        { "VolumeBar", "volume_bar", 60, 12, GROUP_CONFIG, {} },
        { "PopMenu", "pop_menu", 136, 126, GROUP_CONFIG, {} },
        { "ControlsBox", "controls", 140, 160, GROUP_CONFIG, {} },
        { "Secrets", "secret_trophies", 16, 16, GROUP_SECRETS, {} },
    };

    Palette palettes[GROUP_COUNT];
    // Index 0 is transparent; the common colours come first, each page's after them.
    auto colourIndex = [&](Group group, uint16_t c) -> int {
        auto common = palettes[GROUP_COMMON].mLookup.find(c);
        if (common != palettes[GROUP_COMMON].mLookup.end())
        {
            return common->second;
        }
        Palette& p = palettes[group];
        auto it = p.mLookup.find(c);
        if (it != p.mLookup.end())
        {
            return it->second;
        }
        const size_t first = group == GROUP_COMMON ? 1 : 1 + palettes[GROUP_COMMON].mColours.size();
        if (first + p.mColours.size() > 255)
        {
            return -1;
        }
        const uint8_t index = static_cast<uint8_t>(first + p.mColours.size());
        p.mLookup[c] = index;
        p.mColours.push_back(c);
        return index;
    };

    std::string out = "// Auto-generated pause menu data (see menu_compiler.hpp)\nnamespace menu\n{\n";
    char buf[512];
    for (const ColourSpec& c : kColours)
    {
        snprintf(buf, sizeof(buf), "const unsigned char kColour%s = %d;\n", c.mId, colourIndex(GROUP_COMMON, To555(c.mR, c.mG, c.mB)));
        out += buf;
    }
    out += "struct Image\n{\n    unsigned short mW, mH, mFrameW, mFrameH;\n    const unsigned char* mPixels;\n};\n";
    for (const ImageSpec& spec : images)
    {
        std::vector<uint8_t> rgba;
        unsigned w = 0;
        unsigned h = 0;
        const std::string path = paths.FindTexturePath(spec.mTexture);
        if (path.empty() || lodepng::decode(rgba, w, h, path, LCT_RGBA, 8))
        {
            printf("ERROR: menu texture %s not found/readable\n", spec.mTexture);
            return false;
        }
        unsigned outW = w;
        unsigned outH = h;
        auto source = [&](unsigned x, unsigned y) { return &rgba[(y * w + x) * 4]; };
        std::vector<const uint8_t*> pixels;
        if (spec.mFrames.empty())
        {
            for (unsigned y = 0; y < h; y++)
            {
                for (unsigned x = 0; x < w; x++)
                {
                    pixels.push_back(source(x, y));
                }
            }
        }
        else
        {
            const int cols = static_cast<int>(w) / spec.mFrameW;
            outW = static_cast<unsigned>(spec.mFrameW) * static_cast<unsigned>(spec.mFrames.size());
            outH = static_cast<unsigned>(spec.mFrameH);
            for (unsigned y = 0; y < outH; y++)
            {
                for (unsigned x = 0; x < outW; x++)
                {
                    const int f = spec.mFrames[x / spec.mFrameW];
                    pixels.push_back(source((f % cols) * spec.mFrameW + x % spec.mFrameW, (f / cols) * spec.mFrameH + y));
                }
            }
        }
        std::vector<uint8_t> indices;
        for (const uint8_t* p : pixels)
        {
            if (p[3] < 8)
            {
                indices.push_back(0);
                continue;
            }
            // (a see-through pixel, the secrets' shadows: as it looks over the page's background)
            auto over = [&](int c, int bg) { return static_cast<uint8_t>((c * p[3] + bg * (255 - p[3]) + 127) / 255); };
            const int i = colourIndex(spec.mGroup, To555(over(p[0], kPageBg.mR), over(p[1], kPageBg.mG), over(p[2], kPageBg.mB)));
            if (i < 0)
            {
                printf("ERROR: menu palette group %s doesn't fit\n", kGroupNames[spec.mGroup]);
                return false;
            }
            indices.push_back(static_cast<uint8_t>(i));
        }
        snprintf(buf, sizeof(buf), "const unsigned char kPixels%s[%zu] = {", spec.mId, indices.size());
        out += buf;
        out += Hex(indices, 32);
        snprintf(buf, sizeof(buf), "\n};\nconst Image kImg%s = { %u, %u, %d, %d, kPixels%s };\n", spec.mId, outW, outH,
                 spec.mFrameW, spec.mFrameH, spec.mId);
        out += buf;
    }

    // The volume slider's inside and background (1x1 textures)
    for (const char* stem : { "volume_bar_inside", "volume_bar_bg" })
    {
        std::vector<uint8_t> rgba;
        unsigned w = 0;
        unsigned h = 0;
        const std::string path = paths.FindTexturePath(stem);
        if (path.empty() || lodepng::decode(rgba, w, h, path, LCT_RGBA, 8))
        {
            printf("ERROR: menu texture %s not found/readable\n", stem);
            return false;
        }
        snprintf(buf, sizeof(buf), "const unsigned char kColour%s = %d;\n",
                 std::string(stem) == "volume_bar_inside" ? "VolumeInside" : "VolumeBg",
                 colourIndex(GROUP_CONFIG, To555(rgba[0], rgba[1], rgba[2])));
        out += buf;
    }

    // The palettes: kPalette<Group> (the common one from index 1, the others from kFirstPageColour).
    snprintf(buf, sizeof(buf), "const unsigned short kFirstPageColour = %zu;\n", 1 + palettes[GROUP_COMMON].mColours.size());
    out += buf;
    size_t colours = 0;
    for (int g = 0; g < GROUP_COUNT; g++)
    {
        const Palette& p = palettes[g];
        snprintf(buf, sizeof(buf), "const unsigned short kColours%s = %zu;\nconst unsigned short kPalette%s[%zu] = {",
                 kGroupNames[g], p.mColours.size(), kGroupNames[g], p.mColours.empty() ? 1 : p.mColours.size());
        out += buf;
        for (size_t i = 0; i < p.mColours.size(); i++)
        {
            snprintf(buf, sizeof(buf), "%s%u,", (i % 16 == 0) ? "\n    " : "", p.mColours[i]);
            out += buf;
        }
        out += p.mColours.empty() ? "0\n};\n" : "\n};\n";
        colours += p.mColours.size();
    }

    // The minimaps (Minimap.tiles): each map's cells in the visited table (MinimapTracker.interest) from mOffset.
    namespace fs = std::filesystem;
    std::map<std::string, std::vector<std::vector<int>>> minimaps;
    const fs::path dir = fs::path(paths.GetBasePath()) / "MiniMaps";
    if (fs::exists(dir))
    {
        for (const auto& entry : fs::directory_iterator(dir))
        {
            const std::string stem = entry.path().stem().string();
            if (entry.path().extension() != ".csv" || stem.rfind("Minimap_", 0) != 0)
            {
                continue;
            }
            std::ifstream in(entry.path());
            std::string line;
            std::vector<std::vector<int>> rows;
            while (std::getline(in, line))
            {
                std::vector<int> row;
                std::stringstream ss(line);
                std::string cell;
                while (std::getline(ss, cell, ','))
                {
                    if (!cell.empty() && cell != "\r")
                    {
                        row.push_back(std::stoi(cell));
                    }
                }
                if (!row.empty())
                {
                    rows.push_back(row);
                }
            }
            if (!rows.empty())
            {
                minimaps[stem.substr(8)] = rows;
            }
        }
    }
    out += "struct Minimap\n{\n    const char* mMap;\n    unsigned char mW, mH;\n    unsigned short mOffset;\n"
           "    const unsigned char* mTiles;\n};\n";
    int offset = 0;
    std::string table = "const Minimap kMinimaps[] = {\n";
    for (const auto& [name, rows] : minimaps)
    {
        std::vector<uint8_t> tiles;
        const size_t w = rows[0].size();
        for (const auto& row : rows)
        {
            for (size_t x = 0; x < w; x++)
            {
                const int t = x < row.size() ? row[x] : -1;
                tiles.push_back(static_cast<uint8_t>(t < 0 ? 255 : t));
            }
        }
        snprintf(buf, sizeof(buf), "const unsigned char kMinimap%s[%zu] = {", name.c_str(), tiles.size());
        out += buf;
        out += Hex(tiles, 32);
        out += "\n};\n";
        snprintf(buf, sizeof(buf), "    { \"%s\", %zu, %zu, %d, kMinimap%s },\n", name.c_str(), w, rows.size(), offset,
                 name.c_str());
        table += buf;
        offset += static_cast<int>(tiles.size());
    }
    out += table + "};\n";
    snprintf(buf, sizeof(buf), "const int kMinimapCount = %zu;\nconst int kMinimapCells = %d;\n}\n", minimaps.size(), offset);
    out += buf;
    printf("Menu: %zu images, %zu colours, %zu minimaps (%d cells)\n", images.size(), colours, minimaps.size(), offset);
    WriteFileIfChanged("menu_data.h", out);
    return true;
}
