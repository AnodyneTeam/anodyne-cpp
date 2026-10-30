#include "soft_mosaic.hpp"

#include <string.h>

#include "tex_dark.h"
#include "tonc.h"

#include "screen_fx.hpp"

namespace
{
// Hardware mosaic is only used up to this block size; above it the blocks are built in software. The software
// blocks need one palette entry each (255 at most), which is what limits how small they can go: at 11px the playfield
// is 15 columns x 17 rows = 255 blocks.
constexpr int kHardwareMax = 10;
constexpr int kHardwareMaxWide = 16; // (the GBA's biggest)
constexpr int kPlayfieldH = 160;
constexpr int kMaxCols = 15;     // at the smallest software stride
constexpr int kMaxRows = 17;     // (the grid can start above the playfield, see MosaicBgOffsets::originY)
constexpr int kBlocks = kMaxCols * kMaxRows;
constexpr int kTexCbb = 2;       // borrowed VRAM: one solid-colour tile per block, and the affine map
constexpr int kMapSbb = 24;
constexpr int kTileCount = 1 + kBlocks; // tile 0 stays empty, tile n is solid palette index n
constexpr int kTileHalfwords = kTileCount * 64 / 2;
constexpr int kMapHalfwords = 32 * 32 / 2;

alignas(4) uint16_t gSavedTiles[kTileHalfwords] __attribute__((section(".sbss")));
alignas(4) uint16_t gSavedMap[kMapHalfwords] __attribute__((section(".sbss")));
alignas(4) uint16_t gSavedPal[256] __attribute__((section(".sbss")));

bool gActive = false;
uint16_t gDispcnt = 0;
uint16_t gBgcnt[4] = {};
MosaicBgOffsets gOffsets;

uint16_t* TileDst()
{
    return reinterpret_cast<uint16_t*>(&tile_mem[kTexCbb][0]);
}

uint16_t* MapDst()
{
    return reinterpret_cast<uint16_t*>(&se_mem[kMapSbb][0]);
}

// ---- Sampling the real screen at one pixel -------------------------------------------------------------------

// (16-bit fields: 128 of these sit in IWRAM, which the stack shares)
struct Obj final
{
    int16_t x, y, w, h;
    int16_t cx, cy;       // centre of the (possibly double-size) box
    int16_t pa, pb, pc, pd;
    int16_t tile;
    uint8_t prio, bank;
    bool affine;
    bool hflip, vflip;
};

Obj gObjs[128];
int gObjCount = 0;

constexpr uint8_t kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
constexpr uint8_t kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

// The OBJs that can contribute: shown, 4bpp, and not the semi-transparent ones (the additive glow rays).
void CollectObjs()
{
    gObjCount = 0;
    for (int i = 0; i < 128; i++)
    {
        const uint16_t a0 = oam_mem[i].attr0;
        const uint16_t a1 = oam_mem[i].attr1;
        const uint16_t a2 = oam_mem[i].attr2;
        const bool affine = (a0 & 0x100) != 0;
        if ((!affine && (a0 & 0x200)) || (a0 & 0x2000) || ((a0 >> 10) & 3) != 0)
        {
            continue;
        }
        const int shape = a0 >> 14;
        const int size = a1 >> 14;
        if (shape == 3)
        {
            continue;
        }
        Obj& o = gObjs[gObjCount++];
        o.w = kObjW[shape][size];
        o.h = kObjH[shape][size];
        o.affine = affine;
        const bool dbl = affine && (a0 & 0x200);
        o.y = a0 & 0xFF;
        if (o.y >= 160)
        {
            o.y -= 256;
        }
        o.x = a1 & 0x1FF;
        if (o.x >= 256)
        {
            o.x -= 512;
        }
        o.cx = o.x + (dbl ? o.w : o.w / 2);
        o.cy = o.y + (dbl ? o.h : o.h / 2);
        o.hflip = !affine && (a1 & 0x1000);
        o.vflip = !affine && (a1 & 0x2000);
        if (affine)
        {
            const OBJ_AFFINE& m = obj_aff_mem[(a1 >> 9) & 31];
            o.pa = m.pa;
            o.pb = m.pb;
            o.pc = m.pc;
            o.pd = m.pd;
        }
        o.tile = a2 & 0x3FF;
        o.prio = (a2 >> 10) & 3;
        o.bank = a2 >> 12;
    }
}

// The per-block sampling is the hot part (up to 255 blocks a frame, each testing a few layers): ARM code from IWRAM
// rather than Thumb from ROM, like the audio decoder. Only the grid loop below is called from ROM.
#ifdef ANODYNE_SIM
#define MOSAIC_IWRAM
#define MOSAIC_IWRAM_CALL __attribute__((noinline))
#else
#define MOSAIC_IWRAM __attribute__((section(".iwram.mosaic"), target("arm")))
#define MOSAIC_IWRAM_CALL __attribute__((long_call, noinline, section(".iwram.mosaic"), target("arm")))
#endif

MOSAIC_IWRAM bool SampleObj(const Obj& o, int sx, int sy, uint16_t& colour)
{
    int u;
    int v;
    if (o.affine)
    {
        const int dx = sx - o.cx;
        const int dy = sy - o.cy;
        u = ((o.pa * dx + o.pb * dy) >> 8) + o.w / 2;
        v = ((o.pc * dx + o.pd * dy) >> 8) + o.h / 2;
    }
    else
    {
        u = sx - o.x;
        v = sy - o.y;
    }
    if (u < 0 || v < 0 || u >= o.w || v >= o.h)
    {
        return false;
    }
    if (o.hflip)
    {
        u = o.w - 1 - u;
    }
    if (o.vflip)
    {
        v = o.h - 1 - v;
    }
    const int tile = o.tile + (v >> 3) * (o.w >> 3) + (u >> 3); // 1D mapping
    const volatile uint8_t* bytes = reinterpret_cast<const volatile uint8_t*>(&tile_mem[4][tile]);
    const uint8_t b = bytes[(v & 7) * 4 + ((u & 7) >> 1)];
    const int idx = (u & 1) ? (b >> 4) : (b & 15);
    if (idx == 0)
    {
        return false;
    }
    colour = pal_obj_mem[o.bank * 16 + idx];
    return true;
}

// Per-frame constants of an enabled BG, so a sample is just a few lookups.
struct Bg final
{
    const volatile uint8_t* chars;
    int sbBase;
    int hofs, vofs;
    int maskX, maskY;
    bool wide;
    bool bpp8;
    int prio;
};

Bg gBgs[4];

// Draw order, front to back: per priority the OBJs (OAM order) come before the BGs (lowest number first).
struct Layer final
{
    bool isObj;
    int index;
};

Layer gOrder[128 + 4];
int gOrderCount = 0;

void BuildOrder()
{
    gOrderCount = 0;
    for (int prio = 0; prio < 4; prio++)
    {
        for (int i = 0; i < gObjCount; i++)
        {
            if (gObjs[i].prio == prio)
            {
                gOrder[gOrderCount++] = { true, i };
            }
        }
        for (int bg = 0; bg < 4; bg++)
        {
            const uint16_t cnt = gBgcnt[bg];
            if ((gDispcnt & (DCNT_BG0 << bg)) && (cnt & 3) == prio)
            {
                Bg& b = gBgs[bg];
                b.wide = (cnt & 0x4000) != 0;
                b.maskX = b.wide ? 511 : 255;
                b.maskY = (cnt & 0x8000) ? 511 : 255;
                b.chars = reinterpret_cast<const volatile uint8_t*>(&tile_mem[(cnt >> 2) & 3][0]);
                b.sbBase = (cnt >> 8) & 31;
                b.bpp8 = (cnt & 0x80) != 0;
                b.hofs = gOffsets.x[bg];
                b.vofs = gOffsets.y[bg];
                gOrder[gOrderCount++] = { false, bg };
            }
        }
    }
}

MOSAIC_IWRAM inline bool SampleBg(const Bg& b, int sx, int sy, uint16_t& colour)
{
    const int px = (sx + b.hofs) & b.maskX;
    const int py = (sy + b.vofs) & b.maskY;
    const int tx = px >> 3;
    const int ty = py >> 3;
    const int sb = b.sbBase + (tx >= 32 ? 1 : 0) + (ty >= 32 ? (b.wide ? 2 : 1) : 0);
    const uint16_t entry = se_mem[sb][(ty & 31) * 32 + (tx & 31)];
    int u = px & 7;
    int v = py & 7;
    if (entry & 0x400)
    {
        u = 7 - u;
    }
    if (entry & 0x800)
    {
        v = 7 - v;
    }
    const int tile = entry & 0x3FF;
    if (b.bpp8)
    {
        const int idx = b.chars[tile * 64 + v * 8 + u];
        if (idx == 0)
        {
            return false;
        }
        colour = gSavedPal[idx];
        return true;
    }
    const uint8_t byte = b.chars[tile * 32 + v * 4 + (u >> 1)];
    const int idx = (u & 1) ? (byte >> 4) : (byte & 15);
    if (idx == 0)
    {
        return false;
    }
    colour = gSavedPal[(entry >> 12) * 16 + idx];
    return true;
}

// What the screen shows at (sx, sy): the first layer in draw order with an opaque pixel there, of the `count` in
// `order` (indices into gOrder: those that can reach row sy).
MOSAIC_IWRAM uint16_t SampleScreen(int sx, int sy, const uint8_t* order, int count)
{
    uint16_t colour = 0;
    for (int k = 0; k < count; k++)
    {
        const Layer& l = gOrder[order[k]];
        if (l.isObj)
        {
            const Obj& o = gObjs[l.index];
            // Cheap reject first (generous for the rotated ones).
            if (o.affine)
            {
                const int ext = o.w > o.h ? o.w : o.h;
                if (sx < o.cx - ext || sx > o.cx + ext || sy < o.cy - ext || sy > o.cy + ext)
                {
                    continue;
                }
            }
            else if (sx < o.x || sx >= o.x + o.w || sy < o.y || sy >= o.y + o.h)
            {
                continue;
            }
            if (SampleObj(o, sx, sy, colour))
            {
                return colour;
            }
        }
        else if (SampleBg(gBgs[l.index], sx, sy, colour))
        {
            return colour;
        }
    }
    return gSavedPal[0]; // backdrop
}

// ---- Borrowing the display for the software blocks -----------------------------------------------------------
//
// Writing the affine reference point (or the palette) while the screen is being drawn re-latches it part-way down
// the frame and shows the top of the texture again lower on screen, and the sampling takes most of a frame. So each
// frame's blocks are computed into a buffer and only written out at the start of the next frame, in VBlank.

bool gSaved = false;     // palette / VRAM copies taken
// Double buffer: Compute fills one while the VBlank interrupt may still be reading the other.
uint16_t gBlockColours[2][kBlocks];
int gBlockStride[2];
int gBlockOriginX[2];
int gBlockOriginY[2];
int gBlockM[2];  // tiles per block side
alignas(4) uint8_t gMap[2][32 * 32] __attribute__((section(".sbss")));
int gWrite = 0;
volatile int gReady = -1; // buffer index waiting for the next VBlank, or -1

// The VRAM the blocks borrow: copied a frame early when it can be (the last hardware frame has time to spare, and the
// first software one has none). Only put back if the blocks took the display (Mosaic_End), so an early copy that
// isn't used is harmless.
bool gVramSaved = false;

void SaveVram()
{
    memcpy16(gSavedTiles, TileDst(), kTileHalfwords);
    memcpy16(gSavedMap, MapDst(), kMapHalfwords);
    gVramSaved = true;
}

void Save()
{
    gDispcnt = REG_DISPCNT;
    for (int bg = 0; bg < 4; bg++)
    {
        gBgcnt[bg] = REG_BGCNT[bg] & ~BG_MOSAIC;
    }
    TexDark_Off(); // (its plain colours: it stays off until the display is handed back)
    memcpy16(gSavedPal, pal_bg_mem, 256);
    if (!gVramSaved)
    {
        SaveVram();
    }
    gSaved = true;
}

// Takes over the display (VBlank only: it overwrites tiles that the old screen may still be using).
void Begin()
{
    ClearMosaic();
    // Tile n is solid palette index n; block (bx, by) is tile 1 + by * kMaxCols + bx (see Compute for the map).
    memset32(TileDst(), 0, 16);
    for (int n = 1; n < kTileCount; n++)
    {
        memset32(reinterpret_cast<uint8_t*>(TileDst()) + n * 64, static_cast<uint32_t>(n) * 0x01010101u, 16);
    }
    memset32(MapDst(), 0, 32 * 32 / 4);

    REG_BG2CNT = BG_CBB(kTexCbb) | BG_SBB(kMapSbb) | BG_AFF_32x32 | BG_PRIO(0);
    // (the wide view's HUD boxes are windows 0 and 1)
    REG_DISPCNT = DCNT_MODE2 | DCNT_BG2 | (gDispcnt & (gOffsets.viewW > 160 ? DCNT_WIN0 | DCNT_WIN1 : DCNT_WIN0));
    gActive = true;
}

// Samples the screen at the top-left of every block into out[by * kMaxCols + bx].
MOSAIC_IWRAM_CALL
void SampleGrid(int stride, int cols, int rows, const MosaicBgOffsets& off, uint16_t* out)
{
    uint8_t order[128 + 4];
    for (int by = 0; by < kMaxRows; by++)
    {
        // The layers that can reach this row (the same rejects SampleScreen makes, once a row rather than a block).
        const int sy = off.originY + by * stride;
        int count = 0;
        for (int i = 0; i < gOrderCount; i++)
        {
            const Layer& l = gOrder[i];
            if (l.isObj)
            {
                const Obj& o = gObjs[l.index];
                const int ext = o.w > o.h ? o.w : o.h;
                if (o.affine ? (sy < o.cy - ext || sy > o.cy + ext) : (sy < o.y || sy >= o.y + o.h))
                {
                    continue;
                }
            }
            order[count++] = static_cast<uint8_t>(i);
        }
        for (int bx = 0; bx < kMaxCols; bx++)
        {
            uint16_t colour = 0;
            if (bx < cols && by < rows)
            {
                const int sx = off.originX + bx * stride;
                // Outside the playfield is the C# HUD / margin, which the GBA doesn't draw: black.
                if (sx >= off.viewX && sx < off.viewX + off.viewW && sy >= 0 && sy < kPlayfieldH)
                {
                    colour = SampleScreen(sx, sy, order, count);
                }
            }
            out[by * kMaxCols + bx] = colour;
        }
    }
}

void Compute(int stride, const MosaicBgOffsets& offsets)
{
    gOffsets = offsets;
    CollectObjs();
    BuildOrder();
    // The shader's blocks start at the grid origin (top-left of the C# screen) and run on to the right and down;
    // the origin can be above the playfield (the C# draws a 20px HUD header there).
    const int cols = (offsets.viewX + offsets.viewW - offsets.originX + stride - 1) / stride;
    const int rows = (kPlayfieldH - offsets.originY + stride - 1) / stride;
    // Few blocks: make each one m x m tiles so the affine scale (8.8 fixed point) has more precision and the block
    // edges land on the right pixel. The texture is 32 tiles across.
    int m = 32 / (cols > rows ? cols : rows);
    m = m < 1 ? 1 : (m > 8 ? 8 : m);
    // (the block map only changes with the block count and size: most frames reuse the buffer's)
    static int sMapShape[2] = { -1, -1 };
    const int shape = (cols << 16) | (rows << 8) | m;
    uint8_t* map = gMap[gWrite];
    if (sMapShape[gWrite] != shape)
    {
        sMapShape[gWrite] = shape;
        memset(map, 0, 32 * 32);
        for (int by = 0; by < rows; by++)
        {
            for (int bx = 0; bx < cols; bx++)
            {
                const uint8_t id = static_cast<uint8_t>(1 + by * kMaxCols + bx);
                for (int ty = 0; ty < m; ty++)
                {
                    memset(&map[(by * m + ty) * 32 + bx * m], id, static_cast<size_t>(m));
                }
            }
        }
    }
    gBlockM[gWrite] = m;
    SampleGrid(stride, cols, rows, offsets, gBlockColours[gWrite]);
    gBlockStride[gWrite] = stride;
    gBlockOriginX[gWrite] = offsets.originX;
    gBlockOriginY[gWrite] = offsets.originY;
    gReady = gWrite;
    gWrite ^= 1;
}

// Palette and affine registers for one computed buffer. Cheap enough for the VBlank interrupt.
void Present(int buffer)
{
    pal_bg_mem[0] = 0;
    memcpy16(&pal_bg_mem[1], gBlockColours[buffer], kBlocks);
    memcpy16(MapDst(), gMap[buffer], kMapHalfwords);

    // A block is 8 * m texels wide: 8 * m / stride texels per screen pixel. Anchor the mapping at the middle of the
    // screen so the rounding of the scale costs the least at the edges.
    const int stride = gBlockStride[buffer];
    const int texels = 8 * gBlockM[buffer];
    const int scale = (texels * 256) / stride;
    const int u0 = ((120 - gBlockOriginX[buffer]) * texels * 256) / stride;
    const int v0 = ((80 - gBlockOriginY[buffer]) * texels * 256) / stride;
    BG_AFFINE& bg = REG_BG_AFFINE[2];
    bg.pa = static_cast<s16>(scale);
    bg.pb = 0;
    bg.pc = 0;
    bg.pd = static_cast<s16>(scale);
    // The block edges fall exactly on pixel boundaries, so sample the texture at the pixel centre (+half a pixel):
    // otherwise any rounding error in the scale flips an edge by a whole pixel.
    bg.dx = u0 - scale * 120 + scale / 2;
    bg.dy = v0 - scale * 80 + scale / 2;
}
} // namespace

void Mosaic_OnVBlank()
{
    const int buffer = gReady;
    if (gActive && buffer >= 0)
    {
        gReady = -1;
        Present(buffer);
    }
}

void Mosaic_Apply(int pix, const MosaicBgOffsets& offsets)
{
    const int stride = pix >> 8;
    const int hardwareMax = offsets.viewW > 160 ? kHardwareMaxWide : kHardwareMax;
    if (stride <= hardwareMax)
    {
        // (Its grid starts at the screen's corner, so it only lines up with the shader's when the block size divides
        // the playfield position and the grid origin.)
        ApplyMosaic(stride);
        if (stride == hardwareMax && !gSaved && !gVramSaved)
        {
            SaveVram(); // (the software blocks are likely next)
        }
        return;
    }
    if (!gSaved)
    {
        Save();
    }
    if (!gActive)
    {
        if (gReady >= 0)
        {
            // First software frame: take over the display now (we are early in the frame), then the interrupt
            // keeps it up to date.
            Begin();
            const int buffer = gReady;
            gReady = -1;
            Present(buffer);
            return; // (taking over costs about what sampling does: these blocks stay up a frame longer instead)
        }
        else
        {
            ApplyMosaic(hardwareMax); // nothing computed yet: hold the hardware blocks
        }
    }
    Compute(stride, offsets);
}

bool Mosaic_Active()
{
    return gSaved;
}

void Mosaic_End()
{
    if (gActive)
    {
        gActive = false; // the interrupt stops presenting before the display is handed back
        memcpy16(MapDst(), gSavedMap, kMapHalfwords);
        memcpy16(TileDst(), gSavedTiles, kTileHalfwords);
        memcpy16(pal_bg_mem, gSavedPal, 256);
        REG_BG2CNT = gBgcnt[2];
        REG_DISPCNT = gDispcnt;
    }
    gSaved = false;
    gVramSaved = false;
    gReady = -1;
    ClearMosaic();
}
