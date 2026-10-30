#include "pause_draw.h"

#include <string.h>

#include "tex_dark.h"
#include "tonc.h"

#include "dialog.h"
#include "game_state.h"
#include "hud.h"
#include "menu_data.h"
#include "title_data.h"
#include "world.h"

static_assert(menu::kMinimapCells <= kMinimapCells, "the minimaps need more room in gMinimap");

namespace
{
// ---- Minimap ------------------------------------------------------------------------------------------------------
const menu::Minimap* CurrentMinimap()
{
    // (the HUD asks every frame: look the map up again only when it changes)
    static const char* sName = nullptr;
    static const menu::Minimap* sMinimap = nullptr;
    if (gGame.mMapName == sName)
    {
        return sMinimap;
    }
    sName = gGame.mMapName;
    sMinimap = nullptr;
    if (!sName)
    {
        return nullptr;
    }
    for (const menu::Minimap& m : menu::kMinimaps)
    {
        if (strcmp(m.mMap, sName) == 0)
        {
            sMinimap = &m;
            break;
        }
    }
    return sMinimap;
}

// ---- The bitmap ---------------------------------------------------------------------------------------------------
constexpr int kW = 160;
constexpr int kH = 160;
constexpr int kTop = 20;        // HEADER_HEIGHT: UI y of the bitmap's first row
constexpr int kTilesW = kW / 8;
constexpr int kCanvasBytes = kW * kH; // 8bpp tiles 0-399 of charblock 0
constexpr int kBlankTile = kCanvasBytes / 64;
constexpr int kScreenblock = 27; // (in the tileset's VRAM too, given back with it)
constexpr int kViewX = 40;       // the playfield's x on the 240px screen
constexpr int kLineSeparation = 10; // SpriteFont.lineSeparation
constexpr int kCharW = 7;
constexpr int kGlyphTop = 2; // the C# font's glyphs sit 2px down their 8px line

const menu::Image* const kImages[MENU_IMG_COUNT] = {
    &menu::kImgMenuBg,     &menu::kImgPageSelector,    &menu::kImgArrows,   &menu::kImgMinimapTiles,
    &menu::kImgNoneIcon,   &menu::kImgLongIcon,        &menu::kImgWideIcon, &menu::kImgTransformerIcon,
    &menu::kImgEquippedIcon, &menu::kImgJumpShoes,     &menu::kImgTradeItems, &menu::kImgBigKeys,
    &menu::kImgCards,      &menu::kImgCheckBox,        &menu::kImgVolumeBar, &menu::kImgPopMenu,
    &menu::kImgControlsBox, &menu::kImgSecrets,
};

uint8_t ColourIndex(MenuColour c)
{
    switch (c)
    {
    case MENU_WHITE:
        return menu::kColourWhite;
    case MENU_BLACK:
        return menu::kColourBlack;
    case MENU_GREY:
        return menu::kColourGrey;
    case MENU_RED:
        return menu::kColourRed;
    case MENU_VOLUME_INSIDE:
        return menu::kColourVolumeInside;
    default:
        return menu::kColourVolumeBg;
    }
}

enum CommandKind : uint8_t
{
    CMD_IMAGE,
    CMD_TEXT,
    CMD_FILL,
    CMD_MINIMAP,
};

struct Command final
{
    uint8_t mKind;
    uint8_t mArg;   // image / colour
    uint8_t mFlag;  // text: shadow; minimap: player indicator
    uint8_t mPad;
    int16_t mFrame;
    int16_t mX;
    int16_t mY;
    int16_t mW;
    int16_t mH;
    uint32_t mHash; // text: its characters (a buffer can change under the same pointer)
    const char* mText;
};

constexpr int kMaxCommands = 96;

struct MenuDrawState final
{
    bool mOpen;
    bool mValid; // the bitmap shows mDrawn
    uint8_t mGroup;
    uint8_t mDrawnGroup;
    int mCount;
    int mDrawnCount;
    uint16_t mDispcnt;
    uint16_t mBg0cnt;
    uint16_t mBldcnt;
    uint16_t mBldalpha;
    uint16_t mWinin;
    uint16_t mWinout;
    Command mList[kMaxCommands];
    Command mDrawn[kMaxCommands];
    uint16_t mPalette[256];
};

MenuDrawState gMenu __attribute__((section(".sbss"))); // (EWRAM)

uint8_t* Canvas()
{
    return reinterpret_cast<uint8_t*>(tile_mem[0]);
}

// One pixel (VRAM takes halfwords).
void Plot(int x, int y, uint8_t c)
{
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(kW) || static_cast<unsigned>(y) >= static_cast<unsigned>(kH))
    {
        return;
    }
    const int off = ((y >> 3) * kTilesW + (x >> 3)) * 64 + (y & 7) * 8 + (x & 7);
    volatile uint16_t* p = reinterpret_cast<volatile uint16_t*>(Canvas() + (off & ~1));
    const uint16_t v = *p;
    *p = (off & 1) ? static_cast<uint16_t>((v & 0x00FF) | (c << 8)) : static_cast<uint16_t>((v & 0xFF00) | c);
}

void DrawImage(const menu::Image& img, int frame, int x, int y)
{
    const int cols = img.mW / img.mFrameW;
    const int sx = (frame % cols) * img.mFrameW;
    const int sy = (frame / cols) * img.mFrameH;
    if (sy + img.mFrameH > img.mH)
    {
        return;
    }
    for (int r = 0; r < img.mFrameH; r++)
    {
        const uint8_t* row = img.mPixels + (sy + r) * img.mW + sx;
        for (int c = 0; c < img.mFrameW; c++)
        {
            if (row[c])
            {
                Plot(x + c, y + r, row[c]);
            }
        }
    }
}

void DrawGlyph(char ch, int x, int y, uint8_t colour)
{
    const uint32_t* rows = reinterpret_cast<const uint32_t*>(title::kFontTiles) + title::kFontAscii[static_cast<uint8_t>(ch)] * 8;
    for (int r = 0; r < 8; r++)
    {
        const uint32_t w = rows[r];
        for (int c = 0; c < 8; c++)
        {
            if ((w >> (c * 4)) & 15)
            {
                Plot(x + c, y + r, colour);
            }
        }
    }
}

bool HasGlyph(char c)
{
    // (glyph 0 is 'a': any other character on it has none)
    return c > ' ' && static_cast<uint8_t>(c) < 128 && (title::kFontAscii[static_cast<uint8_t>(c)] != 0 || c == 'a');
}

void DrawText(const char* text, int x, int y, uint8_t colour, bool shadow)
{
    for (int pass = shadow ? 0 : 1; pass < 2; pass++)
    {
        int cx = x;
        int cy = y + kGlyphTop + (pass == 0 ? 1 : 0);
        for (const char* c = text; *c; c++)
        {
            if (*c == '\n')
            {
                cx = x;
                cy += kLineSeparation;
                continue;
            }
            if (HasGlyph(*c))
            {
                DrawGlyph(*c, cx, cy, pass == 0 ? menu::kColourBlack : colour);
            }
            cx += kCharW;
        }
    }
}

void DrawMinimap(int x, int y, bool player)
{
    const menu::Minimap* m = CurrentMinimap();
    if (!m)
    {
        return;
    }
    const menu::Image& sheet = menu::kImgMinimapTiles;
    for (int ty = 0; ty < m->mH; ty++)
    {
        for (int tx = 0; tx < m->mW; tx++)
        {
            const int i = ty * m->mW + tx;
            const int seen = gMinimap[m->mOffset + i];
            if (seen > 0 && m->mTiles[i] != 255)
            {
                DrawImage(sheet, m->mTiles[i], x + tx * 7, y + ty * 7);
            }
            if (seen > 1)
            {
                DrawImage(sheet, 18, x + tx * 7, y + ty * 7); // ChestIndicator
            }
        }
    }
    if (player && gGame.mGridX < m->mW && gGame.mGridY < m->mH)
    {
        DrawImage(sheet, 27, x + gGame.mGridX * 7, y + gGame.mGridY * 7); // PlayerIndicator
    }
}

void LoadPalette(uint8_t group)
{
    pal_bg_mem[0] = 0;
    memcpy16(&pal_bg_mem[1], menu::kPaletteCommon, menu::kColoursCommon);
    const uint16_t* pal = nullptr;
    int count = 0;
    switch (group)
    {
    case MENU_GROUP_MAP:
        pal = menu::kPaletteMap;
        count = menu::kColoursMap;
        break;
    case MENU_GROUP_EQUIP:
        pal = menu::kPaletteEquip;
        count = menu::kColoursEquip;
        break;
    case MENU_GROUP_CARDS:
        pal = menu::kPaletteCards;
        count = menu::kColoursCards;
        break;
    case MENU_GROUP_CONFIG:
        pal = menu::kPaletteConfig;
        count = menu::kColoursConfig;
        break;
    case MENU_GROUP_SECRETS:
        pal = menu::kPaletteSecrets;
        count = menu::kColoursSecrets;
        break;
    default:
        break;
    }
    if (count)
    {
        memcpy16(&pal_bg_mem[menu::kFirstPageColour], pal, count);
    }
}

void Redraw()
{
    MenuDrawState& m = gMenu;
    if (!m.mValid || m.mDrawnGroup != m.mGroup)
    {
        LoadPalette(m.mGroup);
    }
    int first = 0;
    if (m.mCount > 0 && m.mList[0].mKind == CMD_IMAGE && m.mList[0].mArg == MENU_IMG_BG && m.mList[0].mX == 0 &&
        m.mList[0].mY == kTop)
    {
        // The background covers it all (opaque): straight in, a tile row at a time.
        const uint8_t* px = menu::kImgMenuBg.mPixels;
        for (int ty = 0; ty < kH / 8; ty++)
        {
            for (int tx = 0; tx < kTilesW; tx++)
            {
                volatile uint16_t* dst = reinterpret_cast<volatile uint16_t*>(Canvas() + (ty * kTilesW + tx) * 64);
                for (int r = 0; r < 8; r++)
                {
                    const uint8_t* src = px + (ty * 8 + r) * kW + tx * 8;
                    for (int k = 0; k < 4; k++)
                    {
                        dst[r * 4 + k] = static_cast<uint16_t>(src[2 * k] | (src[2 * k + 1] << 8));
                    }
                }
            }
        }
        first = 1;
    }
    else
    {
        memset32(Canvas(), 0, kCanvasBytes / 4);
    }
    for (int i = first; i < m.mCount; i++)
    {
        const Command& c = m.mList[i];
        const int y = c.mY - kTop;
        switch (c.mKind)
        {
        case CMD_IMAGE:
            DrawImage(*kImages[c.mArg], c.mFrame, c.mX, y);
            break;
        case CMD_TEXT:
            DrawText(c.mText, c.mX, y, ColourIndex(static_cast<MenuColour>(c.mArg)), c.mFlag != 0);
            break;
        case CMD_FILL:
            for (int r = 0; r < c.mH; r++)
            {
                for (int col = 0; col < c.mW; col++)
                {
                    Plot(c.mX + col, y + r, ColourIndex(static_cast<MenuColour>(c.mArg)));
                }
            }
            break;
        default:
            DrawMinimap(c.mX, y, c.mFlag != 0);
            break;
        }
    }
    memcpy(m.mDrawn, m.mList, sizeof(Command) * static_cast<size_t>(m.mCount));
    m.mDrawnCount = m.mCount;
    m.mDrawnGroup = m.mGroup;
    m.mValid = true;
}

Command* Add(CommandKind kind)
{
    MenuDrawState& m = gMenu;
    if (m.mCount == kMaxCommands)
    {
        return nullptr;
    }
    Command* c = &m.mList[m.mCount++];
    memset(c, 0, sizeof(*c));
    c->mKind = kind;
    return c;
}
} // namespace

void Minimap_Visit(int gx, int gy)
{
    const menu::Minimap* m = CurrentMinimap();
    if (m && gx >= 0 && gx < m->mW && gy >= 0 && gy < m->mH)
    {
        gMinimap[m->mOffset + gy * m->mW + gx] = 1;
    }
}

void Minimap_AddInterest(int gx, int gy, int delta)
{
    const menu::Minimap* m = CurrentMinimap();
    if (m && gx >= 0 && gx < m->mW && gy >= 0 && gy < m->mH)
    {
        uint8_t& cell = gMinimap[m->mOffset + gy * m->mW + gx];
        cell = static_cast<uint8_t>(cell + delta);
    }
}

bool Minimap_Size(int& w, int& h)
{
    const menu::Minimap* m = CurrentMinimap();
    if (!m)
    {
        return false;
    }
    w = m->mW;
    h = m->mH;
    return true;
}

int Minimap_Tile(int x, int y)
{
    const menu::Minimap* m = CurrentMinimap();
    if (!m || x < 0 || x >= m->mW || y < 0 || y >= m->mH || m->mTiles[y * m->mW + x] == 255)
    {
        return -1;
    }
    return m->mTiles[y * m->mW + x];
}

int Minimap_Seen(int x, int y)
{
    const menu::Minimap* m = CurrentMinimap();
    if (!m || x < 0 || x >= m->mW || y < 0 || y >= m->mH)
    {
        return 0;
    }
    return gMinimap[m->mOffset + y * m->mW + x];
}

void MenuDraw_Open()
{
    TexDark_Off(); // (the plain colours, to put back on closing)
    MenuDrawState& m = gMenu;
    if (m.mOpen)
    {
        return;
    }
    m.mOpen = true;
    m.mValid = false;
    m.mCount = 0;
    m.mDispcnt = REG_DISPCNT;
    m.mBg0cnt = REG_BG0CNT;
    m.mBldcnt = REG_BLDCNT;
    m.mBldalpha = REG_BLDALPHA;
    m.mWinin = REG_WININ;
    m.mWinout = REG_WINOUT;
    memcpy16(m.mPalette, pal_bg_mem, 256);
    // The blank tile, and a map of the bitmap's 20x20 tiles (blank around them)
    memset32(Canvas() + kCanvasBytes, 0, 64 / 4);
    for (int ty = 0; ty < 32; ty++)
    {
        for (int tx = 0; tx < 32; tx++)
        {
            se_mem[kScreenblock][ty * 32 + tx] =
                static_cast<SCR_ENTRY>(tx < kTilesW && ty < kH / 8 ? ty * kTilesW + tx : kBlankTile);
        }
    }
}

void MenuDraw_Close()
{
    MenuDrawState& m = gMenu;
    if (!m.mOpen)
    {
        return;
    }
    m.mOpen = false;
    REG_DISPCNT = m.mDispcnt;
    REG_BG0CNT = m.mBg0cnt;
    REG_BLDCNT = m.mBldcnt;
    REG_BLDALPHA = m.mBldalpha;
    REG_WININ = m.mWinin;
    REG_WINOUT = m.mWinout;
    memcpy16(pal_bg_mem, m.mPalette, 256);
    World_ReloadTileset();
}

bool MenuDraw_IsOpen()
{
    return gMenu.mOpen;
}

void MenuDraw_Begin(MenuGroup group)
{
    gMenu.mCount = 0;
    gMenu.mGroup = group;
}

void MenuDraw_Image(MenuImage image, int frame, int x, int y)
{
    if (Command* c = Add(CMD_IMAGE))
    {
        c->mArg = image;
        c->mFrame = static_cast<int16_t>(frame);
        c->mX = static_cast<int16_t>(x);
        c->mY = static_cast<int16_t>(y);
    }
}

void MenuDraw_Text(const char* text, int x, int y, MenuColour colour, bool shadow)
{
    if (!text || !text[0])
    {
        return;
    }
    if (Command* c = Add(CMD_TEXT))
    {
        uint32_t hash = 2166136261u;
        for (const char* s = text; *s; s++)
        {
            hash = (hash ^ static_cast<uint8_t>(*s)) * 16777619u;
        }
        c->mArg = colour;
        c->mFlag = shadow ? 1 : 0;
        c->mX = static_cast<int16_t>(x);
        c->mY = static_cast<int16_t>(y);
        c->mHash = hash;
        c->mText = text;
    }
}

void MenuDraw_Fill(int x, int y, int w, int h, MenuColour colour)
{
    if (Command* c = Add(CMD_FILL))
    {
        c->mArg = colour;
        c->mX = static_cast<int16_t>(x);
        c->mY = static_cast<int16_t>(y);
        c->mW = static_cast<int16_t>(w);
        c->mH = static_cast<int16_t>(h);
    }
}

bool MenuDraw_Minimap(int centreX, int centreY, bool playerIndicator)
{
    const menu::Minimap* m = CurrentMinimap();
    if (!m)
    {
        return false;
    }
    if (Command* c = Add(CMD_MINIMAP))
    {
        // (MapSubstate's int maths)
        c->mX = static_cast<int16_t>(centreX - m->mW * 7 / 2);
        c->mY = static_cast<int16_t>(centreY - m->mH * 7 / 2);
        c->mFlag = playerIndicator ? 1 : 0;
        // (what it shows changes with the seen screens)
        uint32_t hash = 2166136261u;
        for (int i = 0; i < m->mW * m->mH; i++)
        {
            hash = (hash ^ gMinimap[m->mOffset + i]) * 16777619u;
        }
        c->mHash = hash ^ static_cast<uint32_t>(gGame.mGridX * 64 + gGame.mGridY);
    }
    return true;
}

int MenuDraw_TextWidth(const char* text)
{
    int best = 0;
    int n = 0;
    for (const char* c = text; c && *c; c++)
    {
        n = *c == '\n' ? 0 : n + 1;
        best = n > best ? n : best;
    }
    return best * kCharW;
}

void MenuDraw_Present()
{
    MenuDrawState& m = gMenu;
    if (!m.mOpen)
    {
        return;
    }
    REG_DMA0CNT = 0; // (a wave's HBlank scroll)
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0 | DCNT_OBJ | DCNT_OBJ_1D;
    REG_BG0CNT = BG_CBB(0) | BG_SBB(kScreenblock) | BG_8BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_BG0HOFS = static_cast<uint16_t>(-kViewX);
    REG_BG0VOFS = 0;
    REG_MOSAIC = 0;
    if (Dialog_IsOpen())
    {
        // only the text box's blend (Dialog_Open's), not a dark map's
        REG_BLDCNT = (BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP) << 8;
        REG_BLDALPHA = BLDA_BUILD(13, 3);
    }
    else if (!Screen_FlashActive())
    {
        REG_BLDCNT = 0;
    }
    // (the entities stay under the menu; the HUD stays, as the C#'s header does)
    for (int i = kHudOamCount; i < Dialog_FirstReservedOam(); i++)
    {
        obj_hide(&oam_mem[i]);
    }
    if (!m.mValid || m.mDrawnGroup != m.mGroup || m.mDrawnCount != m.mCount ||
        memcmp(m.mDrawn, m.mList, sizeof(Command) * static_cast<size_t>(m.mCount)) != 0)
    {
        Redraw();
    }
}
