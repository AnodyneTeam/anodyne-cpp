#include "sprite_compiler.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "file_utils.hpp"
#include "lodepng.h"
#include "lz77.hpp"

namespace
{
struct Pixel final
{
    uint8_t r, g, b, a;
};

uint16_t ToGba555(const Pixel& p)
{
    return static_cast<uint16_t>((p.r >> 3) | ((p.g >> 3) << 5) | ((p.b >> 3) << 10));
}

int Distance(uint16_t a, uint16_t b)
{
    const int dr = (a & 0x1F) - (b & 0x1F);
    const int dg = ((a >> 5) & 0x1F) - ((b >> 5) & 0x1F);
    const int db = ((a >> 10) & 0x1F) - ((b >> 10) & 0x1F);
    return dr * dr + dg * dg + db * db;
}

bool IsTransparent(const Pixel& p)
{
    return p.a < 128;
}

// Builds a 16 entry palette (index 0 = transparent). More than 15 opaque colours: keep the 15 most used
// and snap the rest to the nearest kept colour.
std::vector<uint16_t> BuildPalette(const std::vector<Pixel>& image, const std::string& name)
{
    std::map<uint16_t, size_t> histogram;
    for (const Pixel& p : image)
    {
        if (!IsTransparent(p))
        {
            histogram[ToGba555(p)]++;
        }
    }

    std::vector<std::pair<uint16_t, size_t>> colours(histogram.begin(), histogram.end());
    std::stable_sort(colours.begin(), colours.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    if (colours.size() > 15)
    {
        printf("WARNING: sprite %s has %zu colours, reducing to 15\n", name.c_str(), colours.size());
        colours.resize(15);
    }

    std::vector<uint16_t> palette(16, 0);
    for (size_t i = 0; i < colours.size(); i++)
    {
        palette[i + 1] = colours[i].first;
    }
    return palette;
}

uint8_t FindPaletteIndex(const std::vector<uint16_t>& palette, const Pixel& p)
{
    if (IsTransparent(p))
    {
        return 0;
    }

    const uint16_t c = ToGba555(p);
    uint8_t best = 1;
    int bestDistance = Distance(c, palette[1]);
    for (size_t i = 1; i < palette.size(); i++)
    {
        const int d = Distance(c, palette[i]);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = static_cast<uint8_t>(i);
        }
        if (d == 0)
        {
            break;
        }
    }
    return best;
}

struct ObjShape
{
    int mTilesW;
    int mTilesH;
    int mShape;
    int mSize;
};

// The 12 hardware OBJ sizes.
const ObjShape kObjShapes[] = {
    { 1, 1, 0, 0 }, { 2, 2, 0, 1 }, { 4, 4, 0, 2 }, { 8, 8, 0, 3 }, { 2, 1, 1, 0 }, { 4, 1, 1, 1 },
    { 4, 2, 1, 2 }, { 8, 4, 1, 3 }, { 1, 2, 2, 0 }, { 1, 4, 2, 1 }, { 2, 4, 2, 2 }, { 4, 8, 2, 3 },
};

// Covers a tilesW x tilesH frame with hardware OBJs, no overlap and no padding: scan row-major and at each
// uncovered tile place the largest shape that fits the free space. Not minimal, but small (a 16x16 frame is one
// part, a 64x80 boss is a handful) and exact, so frame data size is unchanged.
std::vector<SpritePart> LayoutParts(int tilesW, int tilesH)
{
    std::vector<SpritePart> parts;
    std::vector<uint8_t> covered(static_cast<size_t>(tilesW) * tilesH, 0);
    int tileOffset = 0;

    for (int y = 0; y < tilesH; y++)
    {
        for (int x = 0; x < tilesW; x++)
        {
            if (covered[static_cast<size_t>(y) * tilesW + x])
            {
                continue;
            }

            const ObjShape* best = nullptr;
            for (const ObjShape& shape : kObjShapes)
            {
                if (x + shape.mTilesW > tilesW || y + shape.mTilesH > tilesH)
                {
                    continue;
                }
                bool free = true;
                for (int dy = 0; dy < shape.mTilesH && free; dy++)
                {
                    for (int dx = 0; dx < shape.mTilesW; dx++)
                    {
                        if (covered[static_cast<size_t>(y + dy) * tilesW + x + dx])
                        {
                            free = false;
                            break;
                        }
                    }
                }
                if (free && (!best || shape.mTilesW * shape.mTilesH > best->mTilesW * best->mTilesH))
                {
                    best = &shape;
                }
            }

            for (int dy = 0; dy < best->mTilesH; dy++)
            {
                for (int dx = 0; dx < best->mTilesW; dx++)
                {
                    covered[static_cast<size_t>(y + dy) * tilesW + x + dx] = 1;
                }
            }

            SpritePart part;
            part.mTileX = x;
            part.mTileY = y;
            part.mTilesW = best->mTilesW;
            part.mTilesH = best->mTilesH;
            part.mShape = best->mShape;
            part.mSize = best->mSize;
            part.mTileOffset = tileOffset;
            tileOffset += best->mTilesW * best->mTilesH;
            parts.push_back(part);
        }
    }
    return parts;
}

bool CompileSheet(const std::string& pngPath, const SpriteSheetSpec& spec, CompiledSpriteSheet& out)
{
    std::vector<uint8_t> raw;
    unsigned pngW = 0;
    unsigned pngH = 0;
    const unsigned error = lodepng::decode(raw, pngW, pngH, pngPath, LCT_RGBA, 8);
    if (error)
    {
        printf("ERROR: %s: %s\n", pngPath.c_str(), lodepng_error_text(error));
        return false;
    }

    // Soft glows (alpha gradient of one gold colour): drawn as additive semi-transparent OBJs (BLDALPHA 16/16) behind
    // the opaque glow-disc sprite, so they only show outside the disc - like the C# alpha blend of gold over orange,
    // which is nearly invisible on the disc. Store gold * alpha (a little reduced); near-zero alpha is transparent.
    const std::string texName = spec.mTexture;
    if (texName.rfind("door_spinglow", 0) == 0 && texName.size() > 6 && texName.compare(texName.size() - 6, 6, "_inner") == 0)
    {
        // The part of the glow that lies over the orange disc: opaque, alpha-blended against the disc colour like
        // the C# does, and cut to a circle (radius 13) that is inside the disc at every rotation. It is drawn in
        // front of the disc; the outer rays (the plain variant) sit behind it.
        const int disc[3] = { 228, 165, 65 };
        const unsigned w = pngW;
        for (size_t i = 0; i + 3 < raw.size(); i += 4)
        {
            const unsigned px = static_cast<unsigned>((i / 4) % w);
            const unsigned py = static_cast<unsigned>((i / 4) / w);
            const int dx = static_cast<int>(px) - 32;
            const int dy = static_cast<int>(py) - 32;
            const int rawA = raw[i + 3];
            const int a = rawA * 14 / 10 > 255 ? 255 : rawA * 14 / 10;
            if (rawA < 6 || dx * dx + dy * dy > 13 * 13)
            {
                raw[i + 3] = 0;
                continue;
            }
            for (int ch = 0; ch < 3; ch++)
            {
                raw[i + ch] = static_cast<uint8_t>((raw[i + ch] * a + disc[ch] * (255 - a)) / 255);
            }
            raw[i + 3] = 255;
        }
    }
    else if (texName.rfind("door_spinglow", 0) == 0)
    {
        for (size_t i = 0; i + 3 < raw.size(); i += 4)
        {
            const int a = raw[i + 3];
            if (a < 14) // short rays: the C# ones only just reach the floor
            {
                raw[i + 3] = 0;
                continue;
            }
            for (int ch = 0; ch < 3; ch++)
            {
                raw[i + ch] = static_cast<uint8_t>(raw[i + ch] * a * 6 / (255 * 10));
            }
            raw[i + 3] = 255;
        }
    }

    const int fw = spec.mFrameWidth;
    const int fh = spec.mFrameHeight;
    if (fw <= 0 || fh <= 0 || static_cast<unsigned>(fw) > pngW || static_cast<unsigned>(fh) > pngH)
    {
        printf("ERROR: sprite %s: frame size %dx%d doesn't fit %ux%u image\n", spec.mTexture, fw, fh, pngW, pngH);
        return false;
    }

    // Same frame addressing as the C# Spritesheet.GetRect: frames run left to right, top to bottom.
    out.mName = spec.mTexture;
    out.mFrameWidth = fw;
    out.mFrameHeight = fh;
    out.mFramesPerRow = static_cast<int>(pngW) / fw;
    out.mFrameCount = out.mFramesPerRow * (static_cast<int>(pngH) / fh);
    out.mTilesW = (fw + 7) / 8;
    out.mTilesH = (fh + 7) / 8;
    out.mParts = LayoutParts(out.mTilesW, out.mTilesH);
    out.mAnims = spec.mAnims;

    if (static_cast<unsigned>(out.mFramesPerRow * fw) != pngW || static_cast<unsigned>((static_cast<int>(pngH) / fh) * fh) != pngH)
    {
        printf("WARNING: sprite %s: %ux%u image isn't a whole number of %dx%d frames, edge pixels dropped\n",
               spec.mTexture, pngW, pngH, fw, fh);
    }

    std::vector<Pixel> image(static_cast<size_t>(pngW) * pngH);
    for (size_t i = 0; i < image.size(); i++)
    {
        image[i] = { raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2], raw[i * 4 + 3] };
    }

    out.mPalette = BuildPalette(image, spec.mTexture);

    for (const SpriteAnimSpec& anim : spec.mAnims)
    {
        for (int f : anim.mFrames)
        {
            // Variant-relative anims can legitimately run past the end for offset 0; just report real overruns.
            if (f < 0 || f >= out.mFrameCount)
            {
                printf("WARNING: sprite %s anim %s: frame %d outside %d frames\n", spec.mTexture, anim.mName, f,
                       out.mFrameCount);
            }
        }
    }

    out.mTiles.assign(static_cast<size_t>(out.mFrameCount) * out.mTilesW * out.mTilesH * 32, 0);
    size_t pos = 0;
    for (int frame = 0; frame < out.mFrameCount; frame++)
    {
        const int originX = (frame % out.mFramesPerRow) * fw;
        const int originY = (frame / out.mFramesPerRow) * fh;

        for (const SpritePart& part : out.mParts)
        {
            for (int ty = part.mTileY; ty < part.mTileY + part.mTilesH; ty++)
            {
                for (int tx = part.mTileX; tx < part.mTileX + part.mTilesW; tx++)
                {
                    for (int y = 0; y < 8; y++)
                    {
                        for (int x = 0; x < 8; x += 2)
                        {
                            uint8_t nibbles[2] = { 0, 0 };
                            for (int n = 0; n < 2; n++)
                            {
                                const int px = tx * 8 + x + n;
                                const int py = ty * 8 + y;
                                if (px < fw && py < fh)
                                {
                                    const Pixel& p = image[static_cast<size_t>(originY + py) * pngW + originX + px];
                                    nibbles[n] = FindPaletteIndex(out.mPalette, p);
                                }
                            }
                            // 4bpp: low nibble is the left pixel.
                            out.mTiles[pos++] = static_cast<uint8_t>(nibbles[0] | (nibbles[1] << 4));
                        }
                    }
                }
            }
        }
    }

    const size_t frameBytes = static_cast<size_t>(out.mTilesW) * out.mTilesH * 32;
    for (int frame = 0; frame < out.mFrameCount; frame++)
    {
        out.mFrameOffsets.push_back(static_cast<uint32_t>(out.mPacked.size()));
        const std::vector<uint8_t> packed = CompressLz77(out.mTiles.data() + frame * frameBytes, frameBytes);
        out.mPacked.insert(out.mPacked.end(), packed.begin(), packed.end());
    }
    out.mFrameOffsets.push_back(static_cast<uint32_t>(out.mPacked.size()));

    return true;
}

std::string Identifier(const std::string& name)
{
    std::string id = name;
    for (char& c : id)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            c = '_';
        }
    }
    return id;
}

void Append(std::string& s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void Append(std::string& s, const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    s += buf;
}
} // namespace

std::vector<CompiledSpriteSheet> SpriteCompiler::CompileAll(const ContentPaths& contentPaths) const
{
    std::vector<CompiledSpriteSheet> sheets;
    for (const SpriteSheetSpec& spec : GetSpriteSheetSpecs())
    {
        std::string stem = spec.mTexture;
        if (stem.size() > 6 && stem.compare(stem.size() - 6, 6, "_inner") == 0)
        {
            stem.resize(stem.size() - 6); // door_spinglow1_inner bakes door_spinglow1.png differently
        }
        std::string path = contentPaths.FindTexturePath(stem);
        if (std::string(spec.mTexture) == "placeholder")
        {
            // 16x16: magenta frame, dark fill, white cross - obviously "not a real sprite".
            std::vector<unsigned char> px(16 * 16 * 4);
            for (int y = 0; y < 16; y++)
            {
                for (int x = 0; x < 16; x++)
                {
                    const bool border = x == 0 || y == 0 || x == 15 || y == 15;
                    const bool cross = x == y || x + y == 15;
                    unsigned char* p = &px[(y * 16 + x) * 4];
                    p[0] = border ? 255 : (cross ? 255 : 40);
                    p[1] = border ? 0 : (cross ? 255 : 0);
                    p[2] = border ? 255 : (cross ? 255 : 60);
                    p[3] = 255;
                }
            }
            path = "placeholder.png";
            lodepng::encode(path, px, 16, 16);
        }
        if (path.empty())
        {
            printf("ERROR: sprite texture %s.png not found\n", spec.mTexture);
            continue;
        }

        CompiledSpriteSheet sheet;
        if (CompileSheet(path, spec, sheet))
        {
            sheets.push_back(std::move(sheet));
        }
    }

    size_t tileBytes = 0;
    size_t packedBytes = 0;
    for (const auto& s : sheets)
    {
        tileBytes += s.mTiles.size();
        packedBytes += s.mPacked.size() + s.mFrameOffsets.size() * 4;
    }
    printf("Sprite compilation complete: %zu sheets, %zu bytes of 4bpp tile data -> %zu bytes LZ77\n", sheets.size(),
           tileBytes, packedBytes);
    return sheets;
}

void SpriteCompiler::WriteHeaders(const std::vector<CompiledSpriteSheet>& sheets) const
{
    std::string data = "// Auto-generated sprite data (LZ77 4bpp OBJ tiles + 16 colour palettes)\n\n#include <stdint.h>\n\n";
    std::string reg = "// Auto-generated sprite registry\n"
                      "// NOTE: relies on whoever includes this having already included src/anodyne/sprite_data.h\n"
                      "// (for SpriteSheetEntry/SpriteAnimData), same convention as map_registry.h.\n\n"
                      "#include \"sprite_pixels.h\"\n\n"
                      "static const SpriteSheetEntry kSpriteSheets[] =\n{\n";

    for (const CompiledSpriteSheet& s : sheets)
    {
        // A texture can appear at more than one frame size (e.g. fields_npcs 16x16 and 32x32).
        const std::string id = Identifier(s.mName) + "_" + std::to_string(s.mFrameWidth) + "x" + std::to_string(s.mFrameHeight);

        Append(data, "static const unsigned short kSprite_%s_Pal[16] =\n{\n   ", id.c_str());
        for (size_t i = 0; i < s.mPalette.size(); i++)
        {
            Append(data, " 0x%04X,", s.mPalette[i]);
        }
        Append(data, "\n};\n");

        // 32-bit words: the GBA copies tile data with word DMA/CpuFastSet.
        Append(data, "alignas(4) static const unsigned char kSprite_%s_Tiles[%zu] =\n{", id.c_str(), s.mPacked.size());
        for (size_t i = 0; i < s.mPacked.size(); i++)
        {
            Append(data, "%s0x%02X,", (i % 32 == 0) ? "\n    " : "", s.mPacked[i]);
        }
        Append(data, "\n};\n");

        Append(data, "static const uint32_t kSprite_%s_Offsets[%zu] = {", id.c_str(), s.mFrameOffsets.size());
        for (uint32_t offset : s.mFrameOffsets)
        {
            Append(data, " %u,", offset);
        }
        Append(data, " };\n");

        Append(data, "static const SpritePartData kSprite_%s_Parts[] = {", id.c_str());
        for (const SpritePart& part : s.mParts)
        {
            Append(data, " { %d, %d, %d, %d, %d, %d, %d },", part.mTileX * 8, part.mTileY * 8, part.mTilesW * 8,
                   part.mTilesH * 8, part.mShape, part.mSize, part.mTileOffset);
        }
        Append(data, " };\n");

        for (size_t a = 0; a < s.mAnims.size(); a++)
        {
            const SpriteAnimSpec& anim = s.mAnims[a];
            Append(data, "static const unsigned short kSprite_%s_Anim%zu[] = {", id.c_str(), a);
            for (int f : anim.mFrames)
            {
                Append(data, " %d,", f);
            }
            if (anim.mFrames.empty())
            {
                Append(data, " 0,");
            }
            Append(data, " };\n");
        }

        if (s.mAnims.empty())
        {
            Append(data, "static const SpriteAnimData kSprite_%s_Anims[1] = { { nullptr, 0, 0, 0, nullptr } };\n\n", id.c_str());
        }
        else
        {
            Append(data, "static const SpriteAnimData kSprite_%s_Anims[] =\n{\n", id.c_str());
            for (size_t a = 0; a < s.mAnims.size(); a++)
            {
                const SpriteAnimSpec& anim = s.mAnims[a];
                if (anim.mFps != std::floor(anim.mFps) || anim.mFps < 0 || anim.mFps > 255)
                {
                    printf("WARNING: sprite %s anim %s: fps %.2f rounded\n", s.mName.c_str(), anim.mName, anim.mFps);
                }
                Append(data, "    { \"%s\", %zu, %d, %d, kSprite_%s_Anim%zu },\n", anim.mName, anim.mFrames.size(),
                       static_cast<int>(std::lround(anim.mFps)), anim.mLoop ? 1 : 0, id.c_str(), a);
            }
            Append(data, "};\n\n");
        }

        Append(reg, "    { \"%s\", %d, %d, %d, %d, %d, %d, kSprite_%s_Pal, kSprite_%s_Tiles, kSprite_%s_Offsets, kSprite_%s_Parts, %zu, kSprite_%s_Anims, %zu },\n",
               s.mName.c_str(), s.mFrameWidth, s.mFrameHeight, s.mTilesW, s.mTilesH, s.mFramesPerRow, s.mFrameCount,
               id.c_str(), id.c_str(), id.c_str(), id.c_str(), s.mParts.size(), id.c_str(), s.mAnims.size());
    }

    Append(reg, "};\n\nstatic const uint32_t kSpriteSheetCount = sizeof(kSpriteSheets) / sizeof(kSpriteSheets[0]);\n");

    WriteFileIfChanged("sprite_pixels.h", data);
    WriteFileIfChanged("sprite_registry.h", reg);
}
