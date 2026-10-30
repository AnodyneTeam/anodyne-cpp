// Scanline renderer, driven purely by the registers, VRAM, palette RAM and OAM as the game left them when the line
// is drawn: modes 0-5, text BGs (4/8bpp, every size, flips), affine BGs (wrap, internal reference points that
// latch at VBlank and on writes), bitmap modes (page flip), regular/affine/double-size OBJs with 1D/2D mapping,
// OBJ priorities, windows 0/1 and the OBJ window, alpha blending / brighten / darken and semi-transparent OBJs,
// BG and OBJ mosaic, forced blank and the backdrop.

#include "sim_internal.h"

#include <string.h>

namespace sim
{
void Ppu_OnAffineRefWrite(int bg);

namespace
{
constexpr uint16_t kOpaque = 0x8000; // colours are 15 bit: bit 15 marks a drawn pixel

uint32_t gFrame[kScreenW * kScreenH];
uint16_t gBgLine[4][kScreenW];
uint16_t gObjLine[kScreenW];
uint8_t gObjPrio[kScreenW];
uint8_t gObjSemi[kScreenW];
uint8_t gObjWin[kScreenW];

// Affine BG2/BG3 internal reference points (19.8 fixed point), and the copy used for vertical mosaic.
int32_t gRefX[2];
int32_t gRefY[2];
int32_t gMosRefX[2];
int32_t gMosRefY[2];

const uint8_t kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
const uint8_t kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

int32_t SignExtend28(uint32_t v)
{
    return static_cast<int32_t>(v << 4) >> 4;
}

void LoadRef(int i)
{
    const uint32_t base = 0x28 + 0x10 * static_cast<uint32_t>(i);
    gRefX[i] = SignExtend28(Io32(base));
    gRefY[i] = SignExtend28(Io32(base + 4));
}

uint16_t BgColor(int index)
{
    return static_cast<uint16_t>(Pal()[index] & 0x7FFF);
}

uint16_t ObjColor(int index)
{
    return static_cast<uint16_t>(Pal()[256 + index] & 0x7FFF);
}

void RenderTextBg(int bg, int line)
{
    uint16_t* out = gBgLine[bg];
    const uint16_t cnt = Io16(0x08 + 2 * static_cast<uint32_t>(bg));
    const int hofs = Io16(0x10 + 4 * static_cast<uint32_t>(bg)) & 0x1FF;
    const int vofs = Io16(0x12 + 4 * static_cast<uint32_t>(bg)) & 0x1FF;
    const uint32_t cbb = ((cnt >> 2) & 3) * 0x4000u;
    const uint32_t sbb = ((cnt >> 8) & 31) * 0x800u;
    const bool bpp8 = (cnt & 0x80) != 0;
    const int size = cnt >> 14;
    const int w = (size & 1) ? 512 : 256;
    const int h = (size & 2) ? 512 : 256;
    const uint16_t mosaic = Io16(0x4C);
    const bool mos = (cnt & 0x40) != 0;
    const int mosH = mos ? (mosaic & 15) + 1 : 1;
    const int mosV = mos ? ((mosaic >> 4) & 15) + 1 : 1;
    const uint8_t* vram = Vram();

    const int y = line - line % mosV;
    const int sy = (y + vofs) % h;
    for (int x = 0; x < kScreenW; x++)
    {
        const int mx = x - x % mosH;
        const int sx = (mx + hofs) % w;
        uint32_t block = 0;
        if (sx >= 256)
        {
            block += 1;
        }
        if (sy >= 256)
        {
            block += (w == 512) ? 2 : 1;
        }
        const uint32_t seAddr = sbb + block * 0x800u + static_cast<uint32_t>(((sy & 255) >> 3) * 32 + ((sx & 255) >> 3)) * 2u;
        if (seAddr + 2 > 0x10000)
        {
            out[x] = 0;
            continue;
        }
        uint16_t se;
        memcpy(&se, vram + seAddr, 2);
        const uint32_t tile = se & 0x3FF;
        const int px = (sx & 7) ^ ((se & 0x400) ? 7 : 0);
        const int py = (sy & 7) ^ ((se & 0x800) ? 7 : 0);
        if (bpp8)
        {
            const uint32_t addr = cbb + tile * 64 + static_cast<uint32_t>(py * 8 + px);
            const uint8_t idx = addr < 0x10000 ? vram[addr] : 0; // BGs can't fetch from OBJ VRAM
            out[x] = idx ? static_cast<uint16_t>(BgColor(idx) | kOpaque) : 0;
        }
        else
        {
            const uint32_t addr = cbb + tile * 32 + static_cast<uint32_t>(py * 4 + px / 2);
            const uint8_t byte = addr < 0x10000 ? vram[addr] : 0;
            const int nib = (px & 1) ? (byte >> 4) : (byte & 15);
            out[x] = nib ? static_cast<uint16_t>(BgColor((se >> 12) * 16 + nib) | kOpaque) : 0;
        }
    }
}

// Affine BG (mode 1 BG2, mode 2 BG2/BG3) and the bitmap modes (BG2), which use the same transform.
void RenderAffineBg(int bg, int line, int mode)
{
    uint16_t* out = gBgLine[bg];
    const int i = bg - 2;
    const uint32_t base = 0x20 + 0x10 * static_cast<uint32_t>(i);
    const int16_t pa = static_cast<int16_t>(Io16(base));
    const int16_t pc = static_cast<int16_t>(Io16(base + 4));
    const uint16_t cnt = Io16(0x08 + 2 * static_cast<uint32_t>(bg));
    const uint16_t mosaic = Io16(0x4C);
    const bool mos = (cnt & 0x40) != 0;
    const int mosH = mos ? (mosaic & 15) + 1 : 1;
    const int mosV = mos ? ((mosaic >> 4) & 15) + 1 : 1;
    if (line % mosV == 0)
    {
        gMosRefX[i] = gRefX[i];
        gMosRefY[i] = gRefY[i];
    }
    const int32_t refX = mos ? gMosRefX[i] : gRefX[i];
    const int32_t refY = mos ? gMosRefY[i] : gRefY[i];
    const uint8_t* vram = Vram();
    const uint32_t page = (Io16(0) & 0x10) ? 0xA000 : 0;

    for (int x = 0; x < kScreenW; x++)
    {
        const int mx = x - x % mosH;
        int tx = (refX + pa * mx) >> 8;
        int ty = (refY + pc * mx) >> 8;
        uint16_t color = 0;
        if (mode == 0)
        {
            const int size = 128 << (cnt >> 14);
            if (cnt & 0x2000)
            {
                tx &= size - 1;
                ty &= size - 1;
            }
            if (tx >= 0 && ty >= 0 && tx < size && ty < size)
            {
                const uint32_t sbb = ((cnt >> 8) & 31) * 0x800u;
                const uint32_t cbb = ((cnt >> 2) & 3) * 0x4000u;
                const uint32_t mapAddr = sbb + static_cast<uint32_t>((ty >> 3) * (size >> 3) + (tx >> 3));
                const uint8_t tile = mapAddr < 0x10000 ? vram[mapAddr] : 0;
                const uint32_t addr = cbb + tile * 64u + static_cast<uint32_t>((ty & 7) * 8 + (tx & 7));
                const uint8_t idx = addr < 0x10000 ? vram[addr] : 0;
                color = idx ? static_cast<uint16_t>(BgColor(idx) | kOpaque) : 0;
            }
        }
        else if (mode == 3 || mode == 5)
        {
            const int bw = mode == 3 ? 240 : 160;
            const int bh = mode == 3 ? 160 : 128;
            if (tx >= 0 && ty >= 0 && tx < bw && ty < bh)
            {
                uint16_t c;
                memcpy(&c, vram + (mode == 5 ? page : 0) + static_cast<uint32_t>(ty * bw + tx) * 2, 2);
                color = static_cast<uint16_t>((c & 0x7FFF) | kOpaque);
            }
        }
        else // mode 4
        {
            if (tx >= 0 && ty >= 0 && tx < 240 && ty < 160)
            {
                const uint8_t idx = vram[page + static_cast<uint32_t>(ty * 240 + tx)];
                color = idx ? static_cast<uint16_t>(BgColor(idx) | kOpaque) : 0;
            }
        }
        out[x] = color;
    }
}

void RenderObjs(int line, bool bitmapMode)
{
    memset(gObjLine, 0, sizeof(gObjLine));
    memset(gObjPrio, 4, sizeof(gObjPrio));
    memset(gObjSemi, 0, sizeof(gObjSemi));
    memset(gObjWin, 0, sizeof(gObjWin));
    const uint16_t* oam = Oam();
    const uint8_t* vram = Vram();
    const bool map1d = (Io16(0) & 0x40) != 0;
    const uint16_t mosaic = Io16(0x4C);
    const int mosH = ((mosaic >> 8) & 15) + 1;
    const int mosV = ((mosaic >> 12) & 15) + 1;

    for (int i = 0; i < 128; i++)
    {
        const uint16_t a0 = oam[i * 4];
        const uint16_t a1 = oam[i * 4 + 1];
        const uint16_t a2 = oam[i * 4 + 2];
        const int affMode = (a0 >> 8) & 3;
        const int gfx = (a0 >> 10) & 3;
        const int shape = a0 >> 14;
        if (affMode == 2 || gfx == 3 || shape == 3)
        {
            continue;
        }
        const int size = a1 >> 14;
        const int w = kObjW[shape][size];
        const int h = kObjH[shape][size];
        const bool affine = (affMode & 1) != 0;
        const int bw = affMode == 3 ? 2 * w : w;
        const int bh = affMode == 3 ? 2 * h : h;
        const bool mos = (a0 & 0x1000) != 0;
        const int effLine = mos ? line - line % mosV : line;
        const int ly = (effLine - (a0 & 0xFF)) & 0xFF;
        if (ly >= bh)
        {
            continue;
        }
        int x0 = a1 & 0x1FF;
        if (x0 >= 256)
        {
            x0 -= 512;
        }
        const bool bpp8 = (a0 & 0x2000) != 0;
        const uint32_t tileBase = a2 & 0x3FF;
        const int prio = (a2 >> 10) & 3;
        const int palBank = a2 >> 12;
        int16_t pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine)
        {
            const int m = (a1 >> 9) & 31;
            pa = static_cast<int16_t>(oam[m * 16 + 3]);
            pb = static_cast<int16_t>(oam[m * 16 + 7]);
            pc = static_cast<int16_t>(oam[m * 16 + 11]);
            pd = static_cast<int16_t>(oam[m * 16 + 15]);
        }

        for (int sx = 0; sx < bw; sx++)
        {
            int screenX = x0 + sx;
            if (screenX < 0 || screenX >= kScreenW)
            {
                continue;
            }
            const int srcX = mos ? (screenX - screenX % mosH) - x0 : sx;
            int tx;
            int ty;
            if (affine)
            {
                const int dx = srcX - bw / 2;
                const int dy = ly - bh / 2;
                tx = ((pa * dx + pb * dy) >> 8) + w / 2;
                ty = ((pc * dx + pd * dy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h)
                {
                    continue;
                }
            }
            else
            {
                tx = (a1 & 0x1000) ? w - 1 - srcX : srcX;
                ty = (a1 & 0x2000) ? h - 1 - ly : ly;
                if (tx < 0 || tx >= w)
                {
                    continue;
                }
            }
            const int tileStep = bpp8 ? 2 : 1;
            const int rowTiles = map1d ? (w / 8) * tileStep : 32;
            const uint32_t tile = (tileBase + static_cast<uint32_t>((ty >> 3) * rowTiles + (tx >> 3) * tileStep)) & 0x3FF;
            if (bitmapMode && tile < 512)
            {
                continue; // that part of OBJ VRAM holds the bitmap
            }
            uint16_t color;
            if (bpp8)
            {
                const uint32_t ofs = (tile * 32 + static_cast<uint32_t>((ty & 7) * 8 + (tx & 7))) & 0x7FFF;
                const uint8_t idx = vram[0x10000 + ofs];
                if (!idx)
                {
                    continue;
                }
                color = ObjColor(idx);
            }
            else
            {
                const uint32_t ofs = (tile * 32 + static_cast<uint32_t>((ty & 7) * 4 + (tx & 7) / 2)) & 0x7FFF;
                const uint8_t byte = vram[0x10000 + ofs];
                const int nib = (tx & 1) ? (byte >> 4) : (byte & 15);
                if (!nib)
                {
                    continue;
                }
                color = ObjColor(palBank * 16 + nib);
            }
            if (gfx == 2)
            {
                gObjWin[screenX] = 1;
                continue;
            }
            if (!(gObjLine[screenX] & kOpaque) || prio < gObjPrio[screenX])
            {
                gObjLine[screenX] = static_cast<uint16_t>(color | kOpaque);
                gObjPrio[screenX] = static_cast<uint8_t>(prio);
                gObjSemi[screenX] = gfx == 1;
            }
        }
    }
}

bool InRange(int v, int a, int b, int limit)
{
    if (b > limit || a > b)
    {
        b = limit;
    }
    return v >= a && v < b;
}

uint16_t Blend(uint16_t a, uint16_t b, int eva, int evb)
{
    int out = 0;
    for (int shift = 0; shift < 15; shift += 5)
    {
        int c = ((((a >> shift) & 31) * eva + ((b >> shift) & 31) * evb) >> 4);
        c = c > 31 ? 31 : c;
        out |= c << shift;
    }
    return static_cast<uint16_t>(out);
}

uint16_t Brighten(uint16_t a, int evy)
{
    int out = 0;
    for (int shift = 0; shift < 15; shift += 5)
    {
        const int c = (a >> shift) & 31;
        out |= (c + (((31 - c) * evy) >> 4)) << shift;
    }
    return static_cast<uint16_t>(out);
}

uint16_t Darken(uint16_t a, int evy)
{
    int out = 0;
    for (int shift = 0; shift < 15; shift += 5)
    {
        const int c = (a >> shift) & 31;
        out |= (c - ((c * evy) >> 4)) << shift;
    }
    return static_cast<uint16_t>(out);
}

uint32_t ToArgb(uint16_t c)
{
    const uint32_t r = c & 31;
    const uint32_t g = (c >> 5) & 31;
    const uint32_t b = (c >> 10) & 31;
    return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}
} // namespace

void Ppu_OnAffineRefWrite(int bg)
{
    LoadRef(bg - 2);
}

void Ppu_BeginFrame()
{
    // The internal reference points reload from BGxX/BGxY at the start of every frame (VBlank).
    LoadRef(0);
    LoadRef(1);
}

void Ppu_RenderLine(int line)
{
    uint32_t* dst = gFrame + line * kScreenW;
    const uint16_t dispcnt = Io16(0);
    const int mode = dispcnt & 7;

    if (dispcnt & 0x80) // forced blank
    {
        for (int x = 0; x < kScreenW; x++)
        {
            dst[x] = 0xFFFFFFFFu;
        }
    }
    else
    {
        // Which BGs exist in this mode, and how they are drawn (0 = off, 1 = text, 2 = affine/bitmap).
        int kind[4] = { 0, 0, 0, 0 };
        switch (mode)
        {
        case 0:
            kind[0] = kind[1] = kind[2] = kind[3] = 1;
            break;
        case 1:
            kind[0] = kind[1] = 1;
            kind[2] = 2;
            break;
        case 2:
            kind[2] = kind[3] = 2;
            break;
        case 3:
        case 4:
        case 5:
            kind[2] = 2;
            break;
        default:
            break; // modes 6/7 are invalid: only the backdrop
        }
        bool bgOn[4];
        for (int bg = 0; bg < 4; bg++)
        {
            bgOn[bg] = kind[bg] != 0 && (dispcnt & (0x100 << bg));
            if (!bgOn[bg])
            {
                continue;
            }
            if (kind[bg] == 1)
            {
                RenderTextBg(bg, line);
            }
            else
            {
                RenderAffineBg(bg, line, mode >= 3 ? mode : 0);
            }
        }
        const bool objOn = (dispcnt & 0x1000) != 0;
        if (objOn)
        {
            RenderObjs(line, mode >= 3);
        }
        else
        {
            memset(gObjLine, 0, sizeof(gObjLine));
            memset(gObjWin, 0, sizeof(gObjWin));
        }

        const bool win0 = (dispcnt & 0x2000) != 0;
        const bool win1 = (dispcnt & 0x4000) != 0;
        const bool winObj = (dispcnt & 0x8000) != 0 && objOn;
        const uint16_t winH[2] = { Io16(0x40), Io16(0x42) };
        const uint16_t winV[2] = { Io16(0x44), Io16(0x46) };
        const uint16_t winIn = Io16(0x48);
        const uint16_t winOut = Io16(0x4A);
        const bool lineIn0 = win0 && InRange(line, winV[0] >> 8, winV[0] & 0xFF, kScreenH);
        const bool lineIn1 = win1 && InRange(line, winV[1] >> 8, winV[1] & 0xFF, kScreenH);
        const uint16_t bldcnt = Io16(0x50);
        const uint16_t bldalpha = Io16(0x52);
        const int eva = (bldalpha & 31) > 16 ? 16 : (bldalpha & 31);
        const int evb = ((bldalpha >> 8) & 31) > 16 ? 16 : ((bldalpha >> 8) & 31);
        const int evy = (Io16(0x54) & 31) > 16 ? 16 : (Io16(0x54) & 31);
        const int bldMode = (bldcnt >> 6) & 3;
        const uint16_t backdrop = BgColor(0);
        int bgPrio[4];
        for (int bg = 0; bg < 4; bg++)
        {
            bgPrio[bg] = Io16(0x08 + 2 * static_cast<uint32_t>(bg)) & 3;
        }

        for (int x = 0; x < kScreenW; x++)
        {
            // Window: which layers (bits 0-3 BG, 4 OBJ) and whether effects (bit 5) are on at this pixel.
            int enable = 0x3F;
            if (win0 || win1 || winObj)
            {
                enable = winOut & 0x3F;
                if (winObj && gObjWin[x])
                {
                    enable = (winOut >> 8) & 0x3F;
                }
                if (lineIn1 && InRange(x, winH[1] >> 8, winH[1] & 0xFF, kScreenW))
                {
                    enable = (winIn >> 8) & 0x3F;
                }
                if (lineIn0 && InRange(x, winH[0] >> 8, winH[0] & 0xFF, kScreenW))
                {
                    enable = winIn & 0x3F;
                }
            }

            // The two front-most layers: 0-3 BG, 4 OBJ, 5 backdrop.
            int topLayer = 5;
            int secondLayer = 5;
            uint16_t top = backdrop;
            uint16_t second = backdrop;
            int topPrio = 99; // the backdrop is behind everything
            int secondPrio = 99;
            auto consider = [&](int layer, int prio, uint16_t color) {
                if (prio < topPrio)
                {
                    secondLayer = topLayer;
                    second = top;
                    secondPrio = topPrio;
                    topLayer = layer;
                    top = color;
                    topPrio = prio;
                }
                else if (prio < secondPrio)
                {
                    secondLayer = layer;
                    second = color;
                    secondPrio = prio;
                }
            };
            // OBJs go in front of BGs of the same priority; BGs of the same priority by number. Considering the
            // candidates front to back with a strict "<" gives exactly that order.
            for (int p = 0; p < 4; p++)
            {
                if (objOn && (enable & 0x10) && (gObjLine[x] & kOpaque) && gObjPrio[x] == p)
                {
                    consider(4, p * 2, static_cast<uint16_t>(gObjLine[x] & 0x7FFF));
                }
                for (int bg = 0; bg < 4; bg++)
                {
                    if (bgOn[bg] && bgPrio[bg] == p && (enable & (1 << bg)) && (gBgLine[bg][x] & kOpaque))
                    {
                        consider(bg, p * 2 + 1, static_cast<uint16_t>(gBgLine[bg][x] & 0x7FFF));
                    }
                }
            }
            uint16_t out = top;
            const bool effects = (enable & 0x20) != 0;
            const bool secondIsTarget2 = (bldcnt & (0x100 << secondLayer)) != 0;
            if (topLayer == 4 && gObjSemi[x] && secondIsTarget2)
            {
                out = Blend(top, second, eva, evb); // semi-transparent OBJ: always alpha, always a 1st target
            }
            else if (effects && (bldcnt & (1 << topLayer)))
            {
                if (bldMode == 1 && secondIsTarget2)
                {
                    out = Blend(top, second, eva, evb);
                }
                else if (bldMode == 2)
                {
                    out = Brighten(top, evy);
                }
                else if (bldMode == 3)
                {
                    out = Darken(top, evy);
                }
            }
            dst[x] = ToArgb(out);
        }
    }

    // After the line the affine reference points move on by (PB, PD).
    for (int i = 0; i < 2; i++)
    {
        const uint32_t base = 0x20 + 0x10 * static_cast<uint32_t>(i);
        gRefX[i] += static_cast<int16_t>(Io16(base + 2));
        gRefY[i] += static_cast<int16_t>(Io16(base + 6));
    }
}

const uint32_t* Ppu_Frame()
{
    return gFrame;
}
} // namespace sim
