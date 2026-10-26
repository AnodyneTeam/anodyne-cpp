#include "credits.h"

#include <string.h>

#include "tonc.h"

#include "dialog.h"
#include "ecs.h"
#include "game_state.h"
#include "title_data.h"
#include "world.h"

// The C#'s logic runs as it does there, floats and all (the bot's held Accept runs it at 8x, so its timing decides
// the frame the title comes back on). Drawn here: the labels (BG0, the font's 7px glyphs, centred like the C#'s) and
// the save popup. Not yet: the "go" backdrop, the screenies and the cast's sprites.
namespace
{
constexpr int kMaxLabels = 28;
constexpr int kLineSep = 10;   // English font: 8 + 2
constexpr int kCharW = 7;      // its spaceWidth
constexpr int kViewTop = 10;   // the 180px C# screen shows rows 10..170, as on the title
constexpr int kSbText = 31;
constexpr int kCols = 20;      // 160px
constexpr int kRingRows = 32;  // the BG's 256px wrap

struct Credits final
{
    bool mRequested = false;
    float mLabelY[kMaxLabels];
    int mBase[kMaxLabels]; // each label's y relative to the first's
    const char* mText[kMaxLabels];
    const char* mEndText = "";
    float mBgY = 0;
    float mDim = 0;
    bool mStopScroll = false;
    int mStep = 0;
    int mIndex = 0;
    bool mPopup = false;
    bool mInputDelay = true;
    bool mYes = true;
    bool mEndShown = false;
    int mBand[kRingRows]; // which 8px band of the content each ring row holds (INT32_MIN: none)
} gC;

// MathUtilities.MoveTo with GameTimes.DeltaTime scaled by TimeScale.
bool MoveToScaled(float& v, float target, float speed, float dt)
{
    if (v > target)
    {
        v = v - speed * dt < target ? target : v - speed * dt;
    }
    else
    {
        v = v + speed * dt > target ? target : v + speed * dt;
    }
    return v == target;
}

int LineCount(const char* text)
{
    int n = 1;
    for (const char* c = text; *c; c++)
    {
        if (*c == '\n' || *c == '\r')
        {
            n++;
        }
    }
    return n;
}

// The content shown: the labels (y relative to the first's), or the end label alone at the top.
int ContentTop()
{
    return gC.mEndShown ? 0 : static_cast<int>(gC.mLabelY[0]);
}

// Draws one glyph's rows that fall in band `band` (8px of content) into the ring row's tiles.
void BlitGlyph(char c, int x, int y, int band, uint32_t* rowTiles, uint32_t ink, int dy)
{
    const uint32_t* rows = reinterpret_cast<const uint32_t*>(title::kFontTiles) + title::kFontAscii[static_cast<uint8_t>(c)] * 8;
    for (int r = 0; r < 8; r++)
    {
        const int cy = y + r + dy - band * 8;
        if (cy < 0 || cy >= 8 || !rows[r])
        {
            continue;
        }
        const int tx = x >> 3;
        const int sh = (x & 7) * 4;
        const uint32_t low = rows[r] << sh;
        if (tx >= 0 && tx < kCols)
        {
            uint32_t& w = rowTiles[tx * 8 + cy];
            w = (w & ~(low * 15u)) | (low * ink);
        }
        if (sh && tx + 1 < kCols)
        {
            const uint32_t high = rows[r] >> (32 - sh);
            uint32_t& w = rowTiles[(tx + 1) * 8 + cy];
            w = (w & ~(high * 15u)) | (high * ink);
        }
    }
}

// TextWriter.Draw with CenterText: each line at 80 - width / 2, a shadow 1px down.
void DrawText(const char* text, int top, int band, uint32_t* rowTiles)
{
    int line = 0;
    const char* start = text;
    for (const char* c = text;; c++)
    {
        if (*c == '\n' || *c == '\r' || *c == 0)
        {
            const int len = static_cast<int>(c - start);
            const int y = top + line * kLineSep;
            if (y + 9 > band * 8 && y < band * 8 + 8)
            {
                const int x0 = 80 - len * kCharW / 2;
                for (int pass = 0; pass < 2; pass++)
                {
                    for (int i = 0; i < len; i++)
                    {
                        const char ch = start[i];
                        const uint8_t u = static_cast<uint8_t>(ch);
                        // (index 0 is 'a': anything else without a glyph maps there)
                        if (u < 128 && ch != ' ' && (title::kFontAscii[u] != 0 || ch == 'a'))
                        {
                            BlitGlyph(ch, x0 + i * kCharW, y, band, rowTiles, pass == 0 ? 2 : 1, pass == 0 ? 1 : 0);
                        }
                    }
                }
            }
            if (*c == 0)
            {
                break;
            }
            line++;
            start = c + 1;
        }
    }
}

void RenderBand(int band)
{
    const int ring = band & (kRingRows - 1);
    uint32_t* rowTiles = reinterpret_cast<uint32_t*>(&tile_mem[0][ring * kCols]);
    memset(rowTiles, 0, kCols * 32);
    if (gC.mEndShown)
    {
        DrawText(gC.mEndText, 0, band, rowTiles);
    }
    else
    {
        for (int i = 0; i < kMaxLabels; i++)
        {
            // (the last label is hidden with the popup)
            if (i == kMaxLabels - 1 && gC.mPopup)
            {
                continue;
            }
            const int top = gC.mBase[i];
            const int bottom = top + LineCount(gC.mText[i]) * kLineSep;
            if (bottom > band * 8 && top < band * 8 + 8)
            {
                DrawText(gC.mText[i], top, band, rowTiles);
            }
        }
    }
    gC.mBand[ring] = band;
}

void Draw()
{
    // Content y v shows at C# y top + v; the GBA's row g is C# y g + kViewTop.
    const int top = ContentTop();
    const int first = kViewTop - top;
    for (int y = first; y < first + 160 + 8; y += 8)
    {
        const int band = y >= 0 ? y >> 3 : -((-y + 7) >> 3);
        if (gC.mBand[band & (kRingRows - 1)] != band)
        {
            RenderBand(band);
        }
    }
    REG_BG0HOFS = static_cast<uint16_t>(-40);
    REG_BG0VOFS = static_cast<uint16_t>(first);
}

void InvalidateRing()
{
    for (int& b : gC.mBand)
    {
        b = INT32_MIN;
    }
}

// SavePopup.Draw (its box is the menus' panel here).
void ShowPopup()
{
    const char* lines[3] = { Dialog_GetFor("misc", "any", "checkpoint", 0), Dialog_GetFor("misc", "any", "checkpoint", 1),
                             Dialog_GetFor("misc", "any", "checkpoint", 2) };
    // topLeft (40, 20 + (160 - 29) / 2) of the 180px screen
    const int left = 40;
    const int top = 20 + (160 - 29) / 2 - kViewTop;
    const int xs[3] = { left + 5, left + 5 + 14, left + 5 + 14 };
    const int ys[3] = { top, top + 8, top + 16 };
    Dialog_ShowPanel(left, top, 80, 29);
    Dialog_ShowLabels(lines, xs, ys, 3, top, left + 8, top + (gC.mYes ? 10 : 18), true);
}
} // namespace

void Credits_Request()
{
    gC.mRequested = true;
}

bool Credits_TakeRequest()
{
    const bool r = gC.mRequested;
    gC.mRequested = false;
    return r;
}

void Credits_Enter()
{
    gC = Credits();
    // SetState: every effect is deactivated.
    Screen_SetWave(false);
    Screen_SetFadeSparesUi(false);
    Screen_SetFadeWhite(false);
    Screen_SetFade(0);
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D;
    REG_WININ = REG_WINOUT = 0;
    REG_BLDCNT = 0;
    REG_BLDY = 0;
    oam_init(oam_mem, 128);
    pal_bg_mem[0] = CLR_BLACK;
    pal_bg_mem[1] = CLR_WHITE;
    pal_bg_mem[2] = RGB15(4, 4, 4);
    // The labels: 180px apart at least, the first just under the screen.
    int y = 0;
    for (int i = 0; i < kMaxLabels; i++)
    {
        gC.mText[i] = Dialog_GetFor("misc", "any", "ending", i);
        gC.mBase[i] = y;
        gC.mLabelY[i] = static_cast<float>(180 + y);
        const int h = LineCount(gC.mText[i]) * kLineSep + 5;
        y += h > 180 ? h : 180;
    }
    gC.mEndText = Dialog_GetFor("misc", "any", "ending", 25 + 4);
    gC.mBgY = -480 + 180;
    gC.mDim = 0.2f;
    InvalidateRing();
    for (int i = 0; i < 32 * 32; i++)
    {
        se_mem[kSbText][i] = 0;
    }
    for (int r = 0; r < kRingRows; r++)
    {
        for (int c = 0; c < kCols; c++)
        {
            se_mem[kSbText][r * 32 + c] = static_cast<uint16_t>(r * kCols + c);
        }
    }
    REG_BG0CNT = BG_CBB(0) | BG_SBB(kSbText) | BG_4BPP | BG_REG_32x32 | BG_PRIO(1);
    Draw();
    REG_DISPCNT = REG_DISPCNT | DCNT_BG0;
}

bool Credits_Update()
{
    float timeScale = 1;
    if (!gC.mStopScroll)
    {
        timeScale = key_is_down(KEY_A) ? 8.0f : 1.0f;
        const float speed = 15 * (kDeltaTime * timeScale);
        for (int i = 0; i < kMaxLabels; i++)
        {
            gC.mLabelY[i] = gC.mLabelY[i] + -speed;
            if (i == kMaxLabels - 1 && gC.mLabelY[i] <= 0)
            {
                gC.mLabelY[i] = 0;
                gC.mStopScroll = true;
            }
        }
    }
    const float dt = kDeltaTime * timeScale;

    // StateLogic
    switch (gC.mStep)
    {
    case 0:
        Game_PlaySong("ending");
        gC.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!MoveToScaled(gC.mBgY, 0, 15, dt))
        {
            break;
        }
        gC.mIndex = 6;
        gC.mStep = 2;
        [[fallthrough]];
    case 2:
        if (gC.mIndex < kMaxLabels)
        {
            // ProgressScreenie
            if (!(gC.mLabelY[gC.mIndex] > 180))
            {
                gC.mIndex++;
            }
            break;
        }
        gC.mStep = 3;
        [[fallthrough]];
    case 3:
        if (!MoveToScaled(gC.mDim, 0, 0.06f, dt))
        {
            break;
        }
        gC.mStep = 4;
        [[fallthrough]];
    case 4:
        if (gC.mStopScroll)
        {
            break;
        }
        gC.mStep = 5;
        [[fallthrough]];
    case 5:
        if (!key_hit(KEY_A))
        {
            break;
        }
        gC.mPopup = true;
        gC.mEndShown = true;
        InvalidateRing();
        ShowPopup();
        gC.mStep = 6;
        break;
    default:
        break;
    }

    // SavePopup.Update
    bool done = false;
    if (gC.mPopup)
    {
        if (gC.mInputDelay)
        {
            gC.mInputDelay = false;
        }
        else if (key_hit(KEY_UP) && !gC.mYes)
        {
            gC.mYes = true;
            ShowPopup();
        }
        else if (key_hit(KEY_DOWN) && gC.mYes)
        {
            gC.mYes = false;
            ShowPopup();
        }
        else if (key_hit(KEY_A))
        {
            if (gC.mYes)
            {
                Event_Set("SeenCredits", 1);
                // TODO: GlobalState.SaveGame() (the port has no saves yet)
            }
            Game_PlaySfx("menu_select");
            done = true;
        }
    }
    Draw();
    return done;
}

void Credits_Exit()
{
    Dialog_HideLabels();
    Dialog_HidePanel();
    REG_DISPCNT = REG_DISPCNT & ~DCNT_BG0;
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
}
