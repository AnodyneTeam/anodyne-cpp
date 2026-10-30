#include "title.h"

#include <stdio.h>
#include <string.h>

#include "tonc.h"

#include "audio_manager.h"
#include "dialog.h"
#include "ecs.h"
#include "game_state.h"
#include "hud_data.h"
#include "save.h"
#include "screen_fx.hpp"
#include "soft_mosaic.hpp"
#include "sprite.h"
#include "title_data.h"

namespace
{
constexpr int kViewX = 40;       // the 160px playfield is centred on the 240px screen
constexpr int kViewY = -15;      // the C# screen is 180px tall; show rows 15..175 (title text to "press enter")
constexpr int kMenuViewY = -20;  // the main menu shows rows 20..180 (its labels run from 31 to 176)
constexpr int kSbBg = 28;
constexpr int kSbDoor = 29;
constexpr int kSbText = 27;
constexpr int kCbbText = 2;      // the text canvas: 4bpp tiles 1-400 cover the playfield (CBB 0-1 hold the title art)
constexpr int kTextBank = 15;    // its colours: 1 the credits, 2 the menu's grey, 3 white, 4 and 11-15 health
constexpr int kInkCredits = 1;
constexpr int kInkGrey = 2;
constexpr int kInkWhite = 3;
constexpr int kCharW = 7;        // FontManager.GetCharacterWidth (English)
constexpr int kGlyphTop = 2;     // the C# font's glyphs sit 2px down their 8px line
constexpr int kBgScrollSpeed = 30;     // px/s (ScrollingTex velocity (0, -30))
constexpr int kDoorRise = 20 * 65536 / 60; // 16.16 px a frame (nexusImage velocity -20 px/s)
constexpr int kDoorEndY = 180 - 116;   // door top when fully risen
constexpr int kBlinkFrames = 60;       // PressEnterTimer: 1s

constexpr int kMenuItems = 4;
// Affine matrix slot n lives in the attr3 words of OAM entries 4n..4n+3, so use the last slots (OAM 120-127 unused).
constexpr int kSpinAffine1 = 30;
constexpr int kSpinAffine2 = 31;

enum class State
{
    IntroFade,      // fading in from black (2s), nothing written yet
    CreditsWrite,   // "Original game by..." typed out letter by letter in random order
    CreditsFade,    // to 30% at 0.6/s
    CreditsFadeEnd, // to 0 at 2/s, 1s
    ScrollUp,       // door scrolls up
    Title,          // DisplayTitle: title text + blinking "press enter" (flash and white glint fading out)
    Pixelate,       // mosaic grows while dimming slightly
    FadeOut,        // mosaic keeps growing while fading to black, then the menu
    Menu,           // MainMenuState
    LoadFade        // FileSubstate.LoadedSave: black at 0.72/s, then the intro or the game
};

State gState = State::IntroFade;
int gTimer = 0;
int gBgScroll = 0;       // Fx 24.8, px scrolled
int gBgTopRow = -1;
int gDoorY = 180 << 16;  // C# coordinates, 16.16
int gPix = 256;          // C# Pixelation, 8.8 (block size in px; 1.0 = off)
int gDark = 0;           // C# black_overlay alpha, 16.16
int gFlash = 0;          // C# flash alpha, 16.16 (gFlashWhite: towards white, else black)
int gFlashStep = 0;      // its fall a frame
bool gFlashWhite = false;
int gGlint = 0;          // titleOverlay / subtitleOverlay opacity, 16.16
int gBlinkTimer = 0;
bool gPressVisible = true;

Sprite gTitleText;
Sprite gRemake;
Sprite gPressEnter;
Sprite gArrow;
Sprite gSubArrow;
Sprite gGlow;
Sprite gSpin1;
Sprite gInner1;
Sprite gInner2;
Sprite gSpin2;
bool gHaveText = false;
bool gHaveRemake = false;
bool gHavePress = false;
bool gHaveArrow = false;
bool gHaveSubArrow = false;
bool gHaveGlow = false;
bool gHaveSpin = false;
bool gHaveInner = false;
int gSpinAngle = 0;

void PlaySfx(const char* name)
{
    Game_PlaySfx(name);
}

// Semi-transparent OBJs (the door's spinning glow) add onto BG2/BG3/OBJ/backdrop at full weight.
void ApplyBlend()
{
    REG_BLDCNT = BLD_STD | ((BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP) << 8);
    REG_BLDALPHA = BLDA_BUILD(16, 16);
}

// The full-screen effects over everything: the black overlay, then the flash (white or black) over it. The GBA has
// one brightness fade, so the stronger of the two shows.
void ApplyFade()
{
    const int dark = (gDark * 16 + 32768) >> 16;
    const int flash = (gFlash * 16 + 32768) >> 16;
    if (flash > 0 && (gFlashWhite ? flash >= dark : true))
    {
        REG_BLDCNT = ((gFlashWhite ? 2 : 3) << 6) | 0x3F;
        REG_BLDY = static_cast<uint16_t>(gFlashWhite ? flash : (flash > dark ? flash : dark));
    }
    else if (dark > 0)
    {
        REG_BLDCNT = (3 << 6) | 0x3F;
        REG_BLDY = static_cast<uint16_t>(dark);
    }
    else
    {
        ApplyBlend();
    }
}

// GlobalState.flash.Flash(seconds, colour): instantly full, then fading out.
void StartFlash(int frames, bool white)
{
    gFlash = 65536;
    gFlashStep = 65536 / frames;
    gFlashWhite = white;
}

void UpdateFlash()
{
    gFlash = gFlash > gFlashStep ? gFlash - gFlashStep : 0;
}

// ---- The text canvas (UILabel): BG1, a 160x160 bitmap of 4bpp tiles -------------------------------------------------
uint32_t* CanvasTiles()
{
    return reinterpret_cast<uint32_t*>(&tile_mem[kCbbText][1]);
}

void Canvas_Clear()
{
    memset32(CanvasTiles(), 0, 400 * 8);
}

void Canvas_Init()
{
    memset32(&tile_mem[kCbbText][0], 0, 8);
    Canvas_Clear();
    for (int ty = 0; ty < 32; ty++)
    {
        for (int tx = 0; tx < 32; tx++)
        {
            const int cx = tx - kViewX / 8;
            se_mem[kSbText][ty * 32 + tx] = static_cast<SCR_ENTRY>(
                cx >= 0 && cx < 20 && ty < 20 ? (1 + ty * 20 + cx) | (kTextBank << 12) : 0);
        }
    }
}

void Canvas_Plot(int x, int y, int ink)
{
    if (static_cast<unsigned>(x) >= 160u || static_cast<unsigned>(y) >= 160u)
    {
        return;
    }
    volatile uint32_t* row = CanvasTiles() + ((y >> 3) * 20 + (x >> 3)) * 8 + (y & 7);
    const int shift = (x & 7) * 4;
    *row = (*row & ~(15u << shift)) | (static_cast<uint32_t>(ink) << shift);
}

bool HasGlyph(char c)
{
    // (glyph 0 is 'a': any other character on it has none)
    return c > ' ' && static_cast<uint8_t>(c) < 128 && (title::kFontAscii[static_cast<uint8_t>(c)] != 0 || c == 'a');
}

// One character of a label whose text starts at (x, y) of the C# screen, shown with the screen's row `viewY` at
// the top of the GBA's.
void Canvas_Char(char ch, int x, int y, int ink, int viewY)
{
    if (!HasGlyph(ch))
    {
        return;
    }
    const uint32_t* rows = reinterpret_cast<const uint32_t*>(title::kFontTiles) + title::kFontAscii[static_cast<uint8_t>(ch)] * 8;
    for (int r = 0; r < 8; r++)
    {
        const uint32_t w = rows[r];
        for (int c = 0; c < 8; c++)
        {
            if ((w >> (c * 4)) & 15)
            {
                Canvas_Plot(x + c, y + viewY + kGlyphTop + r, ink);
            }
        }
    }
}

void Canvas_Text(const char* text, int x, int y, int ink, int viewY)
{
    for (int i = 0; text[i]; i++)
    {
        Canvas_Char(text[i], x + i * kCharW, y, ink, viewY);
    }
}

void SetCreditsOpacity(int op) // 16.16
{
    auto ch = [op](int v) { const int c = ((v * op >> 16) + 4) >> 3; return c > 31 ? 31 : c; };
    pal_bg_mem[kTextBank * 16 + kInkCredits] = RGB15(ch(68), ch(109), ch(113)); // Color(68, 109, 113) * opacity
}

// ---- background, door, sprites ---------------------------------------------------------------------------------------
void WriteBgRow(int row)
{
    const int imageRow = row % title::kBgTilesH;
    for (int x = 0; x < 32; x++)
    {
        se_mem[kSbBg][(row & 31) * 32 + x] = x < title::kBgTilesW ? title::kBgMap[imageRow * title::kBgTilesW + x] : 0;
    }
}

void UpdateBg(bool advance)
{
    if (advance)
    {
        gBgScroll += kBgScrollSpeed * 256 / 60;
    }
    const int px = gBgScroll >> 8;
    const int top = px >> 3;
    if (top != gBgTopRow)
    {
        // The screen shows image rows (px - kViewY) / 8 .. (px - kViewY + 159) / 8, within top..top+23: fill any that
        // are new (the map is a ring of 32 rows).
        const int first = (gBgTopRow < 0) ? top : gBgTopRow + 24;
        const int last = top + 23;
        for (int r = (first > last - 31 ? first : last - 31); r <= last; r++)
        {
            WriteBgRow(r);
        }
        gBgTopRow = top;
    }
    REG_BG3HOFS = static_cast<uint16_t>(-kViewX);
    REG_BG3VOFS = static_cast<uint16_t>(px - kViewY);
}

int DoorY()
{
    return gDoorY >> 16;
}

void SetDoorY(int y16)
{
    gDoorY = y16;
    REG_BG2HOFS = static_cast<uint16_t>(-kViewX);
    REG_BG2VOFS = static_cast<uint16_t>(-(DoorY() + kViewY));
}

void HideAllSprites()
{
    Sprite* all[] = { &gTitleText, &gRemake, &gPressEnter, &gArrow, &gSubArrow, &gGlow, &gSpin1, &gSpin2, &gInner1, &gInner2 };
    for (Sprite* s : all)
    {
        if (s->mSheet)
        {
            Sprite_Hide(*s);
        }
    }
}

// titleOverlay / subtitleOverlay: the white copies of the title over it, fading out. Both are the same shape as the
// title art (title_remake_white leaves out its black outline), so the title's colours are tinted to white instead.
void TintTitle(int op) // 16.16
{
    const Sprite* const sprites[2] = { &gTitleText, &gRemake };
    for (int k = 0; k < 2; k++)
    {
        const Sprite& s = *sprites[k];
        if (!s.mSheet)
        {
            continue;
        }
        for (int i = 1; i < 16; i++)
        {
            const uint16_t c = s.mSheet->mPalette[i];
            if (k == 1 && c == 0)
            {
                pal_obj_bank[s.mPalBank][i] = c; // (the outline)
                continue;
            }
            uint16_t out = 0;
            for (int ch = 0; ch < 3; ch++)
            {
                const int v = (c >> (5 * ch)) & 31;
                out |= static_cast<uint16_t>((v + (((31 - v) * op + 32768) >> 16)) << (5 * ch));
            }
            pal_obj_bank[s.mPalBank][i] = out;
        }
    }
}

void DrawTitleSprites()
{
    if (gHaveText) { Sprite_Draw(gTitleText, kViewX + 16, 16 + kViewY); }
    if (gHaveRemake) { Sprite_Draw(gRemake, kViewX + 45, 47 + kViewY); }
    if (gHaveGlow) { Sprite_Draw(gGlow, kViewX + 48, kDoorEndY + 17 + kViewY); }
    if (gHaveSpin)
    {
        // doorSpin1/2 spin at +-90 degrees/s about the centre of a 64x64 image at (48, door top).
        gSpinAngle = (gSpinAngle + 65536 / 4 / 60) & 0xFFFF;
        obj_aff_rotscale(&obj_aff_mem[kSpinAffine1], 256, 256, static_cast<u16>(gSpinAngle));
        obj_aff_rotscale(&obj_aff_mem[kSpinAffine2], 256, 256, static_cast<u16>(-gSpinAngle));
        Sprite_DrawAffine(gSpin1, kViewX + 80, kDoorEndY + 33 + kViewY, kSpinAffine1, true);
        Sprite_DrawAffine(gSpin2, kViewX + 80, kDoorEndY + 33 + kViewY, kSpinAffine2, true);
        if (gHaveInner)
        {
            Sprite_DrawAffine(gInner1, kViewX + 80, kDoorEndY + 33 + kViewY, kSpinAffine1);
            Sprite_DrawAffine(gInner2, kViewX + 80, kDoorEndY + 33 + kViewY, kSpinAffine2);
        }
    }
    if (gHavePress)
    {
        if (gPressVisible)
        {
            Sprite_Draw(gPressEnter, kViewX + (160 - 96) / 2, 160 + kViewY);
        }
        else
        {
            Sprite_Hide(gPressEnter);
        }
    }
}

// ---- credits (TitleState CreditsWrite/CreditsFade): two sets of three lines typed out in random order ----
constexpr int kMaxCredit = 32;
int gCredSet = 0;
int gCredOpacity = 65536; // 16.16
int gCredX[3];
int gCredY[3];
int gCredRemaining = 0;
uint8_t gCredOrder[3 * kMaxCredit][2];
uint32_t gCredRng = 0x2545F491;

void CreditsStart(int set)
{
    gCredSet = set;
    gCredOpacity = 65536;
    gCredRemaining = 0;
    SetCreditsOpacity(gCredOpacity);
    Canvas_Clear();
    for (int i = 0; i < 3; i++)
    {
        // TitleState CreditsWrite.Enter: centred on 80, lines at 88 - lineH - 4, 88 and 88 + lineH (lineH 10)
        const char* line = title::kCreditLines[set * 3 + i];
        const int len = static_cast<int>(strlen(line));
        gCredX[i] = 80 - len * kCharW / 2;
        gCredY[i] = i == 0 ? 88 - 10 - 4 : (i == 1 ? 88 : 98);
        // (every character takes a turn, the spaces too)
        for (int c = 0; c < len && c < kMaxCredit; c++)
        {
            gCredOrder[gCredRemaining][0] = static_cast<uint8_t>(i);
            gCredOrder[gCredRemaining][1] = static_cast<uint8_t>(c);
            gCredRemaining++;
        }
    }
}

// ---- main menu (MainMenuState + FileSubstate) ---------------------------------------------------------------------
enum class FileItem
{
    Game,
    NewGame,
    Yes,
    No,
    Copy,
    Save1,
    Save2,
    Save3,
    Cancel
};

enum class FileView
{
    Normal,
    Confirm,
    CopyTo,
};

int gMenuIndex = 0;      // MainMenuState._state (it's static there: the menu keeps it)
bool gInSub = false;
FileItem gItem = FileItem::Game;
FileView gView = FileView::Normal;
int gConfirm = 0;        // confirmState
bool gCopyMode = false;
int gCopyTarget = 0;
bool gHaveSave = false;
SaveInfo gInfo;
bool gMenuDirty = true;

const char* TitleLine(int id)
{
    return Dialog_Peek("misc", "any", "title", id);
}

// A label with the keyboard keys ([SOMEKEY-C] / [SOMEKEY-X]) as the GBA's buttons.
void ButtonText(char* out, int cap, const char* text)
{
    int n = 0;
    while (*text && n < cap - 1)
    {
        if (strncmp(text, "[SOMEKEY-C]", 11) == 0)
        {
            out[n++] = 'A';
            text += 11;
        }
        else if (strncmp(text, "[SOMEKEY-X]", 11) == 0)
        {
            out[n++] = 'B';
            text += 11;
        }
        else
        {
            out[n++] = *text++;
        }
    }
    out[n] = 0;
}

void LoadFile()
{
    gHaveSave = gMenuIndex < 3 && Save_Peek(gMenuIndex, gInfo);
    gItem = FileItem::Game;
    gView = FileView::Normal;
    gConfirm = 0;
    gCopyMode = false;
    gMenuDirty = true;
}

constexpr int kFileX = 60;
constexpr int kFileY = 28;
constexpr int kFileStep = 12;

int ItemY(FileItem item)
{
    switch (item)
    {
    case FileItem::NewGame: return kFileY + kFileStep;
    case FileItem::Copy: return kFileY + kFileStep * 2;
    case FileItem::No: return kFileY + kFileStep;
    case FileItem::Yes: return kFileY + kFileStep * 2;
    case FileItem::Save1: return kFileY + kFileStep;
    case FileItem::Save2: return kFileY + kFileStep * 2;
    case FileItem::Save3: return kFileY + kFileStep * 3;
    case FileItem::Cancel: return kFileY + kFileStep * 4;
    default: return kFileY;
    }
}

void DrawMenu()
{
    if (!gMenuDirty)
    {
        return;
    }
    gMenuDirty = false;
    Canvas_Clear();
    // MainMenuState.SetLabels: x 10, from y 31 every 16px, Color(116, 140, 144)
    static const char* const kSlots[3] = { "1", "2", "3" };
    for (int i = 0; i < kMenuItems; i++)
    {
        Canvas_Text(i < 3 ? kSlots[i] : Dialog_Peek("misc", "any", "config", 0), 10, 31 + i * 16, kInkGrey, kMenuViewY);
    }
    char buf[64];
    char a[32];
    char b[32];
    ButtonText(a, sizeof(a), Dialog_Peek("misc", "any", "secrets", 14));
    ButtonText(b, sizeof(b), Dialog_Peek("misc", "any", "secrets", 15));
    snprintf(buf, sizeof(buf), "%s %s", a, b);
    Canvas_Text(buf, 2, 168, kInkGrey, kMenuViewY);
    if (gMenuIndex >= 3)
    {
        return; // (ConfigSubstate: not on the title yet)
    }
    // FileSubstate.SetLabels: x 60, from y 28 every 12px
    if (gView == FileView::Normal)
    {
        Canvas_Text(TitleLine(gHaveSave ? 11 : 12), kFileX, kFileY, kInkGrey, kMenuViewY);
        if (gHaveSave)
        {
            Canvas_Text(TitleLine(12), kFileX, kFileY + kFileStep, kInkGrey, kMenuViewY);
            Canvas_Text(TitleLine(25), kFileX, kFileY + kFileStep * 2, kInkGrey, kMenuViewY);
        }
    }
    else if (gView == FileView::Confirm)
    {
        Canvas_Text(TitleLine(13 + gConfirm * 3), kFileX, kFileY, kInkGrey, kMenuViewY);
        Canvas_Text(TitleLine(14 + gConfirm * 3), kFileX, kFileY + kFileStep, kInkGrey, kMenuViewY);
        Canvas_Text(TitleLine(15 + gConfirm * 3), kFileX, kFileY + kFileStep * 2, kInkGrey, kMenuViewY);
    }
    else
    {
        Canvas_Text(TitleLine(27), kFileX, kFileY, kInkGrey, kMenuViewY);
        for (int i = 0; i < 3; i++)
        {
            snprintf(buf, sizeof(buf), "%s%d", TitleLine(24), i + 1);
            Canvas_Text(buf, kFileX, kFileY + kFileStep * (i + 1), i == gMenuIndex ? kInkWhite : kInkGrey, kMenuViewY);
        }
        Canvas_Text(TitleLine(26), kFileX, kFileY + kFileStep * 4, kInkGrey, kMenuViewY);
    }
    if (gHaveSave)
    {
        // the time ("hh:mm:ss"), deaths and cards, in white
        const uint32_t secs = gInfo.mPlayFrames / 60;
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", static_cast<unsigned>(secs / 3600),
                 static_cast<unsigned>(secs / 60 % 60), static_cast<unsigned>(secs % 60));
        Canvas_Text(buf, kFileX, kFileY + kFileStep * 7, kInkWhite, kMenuViewY);
        // ("{n} " + " deaths  ": the compiled lines lose their leading space)
        snprintf(buf, sizeof(buf), "%d %s", gInfo.mDeaths, TitleLine(22));
        Canvas_Text(buf, kFileX, kFileY + kFileStep * 8, kInkWhite, kMenuViewY);
        snprintf(buf, sizeof(buf), "%d %s", gInfo.mCards, TitleLine(23));
        Canvas_Text(buf, kFileX, kFileY + kFileStep * 9, kInkWhite, kMenuViewY);
    }
    if (gHaveSave && gView != FileView::CopyTo)
    {
        // HealthBar at (128, y + 5 steps): rows of 8 boxes leftwards from its right end, full ones first
        for (int i = 0; i < gInfo.mMaxHealth && i < 16; i++)
        {
            const int x = 128 - 11 - 8 * (7 - i % 8) - 7 * (i / 8);
            const int y = kFileY + kFileStep * 5 + i / 8 * 7;
            const hud::Image& img = hud::kImgHealth;
            const int sx = (i < gInfo.mCurHealth ? 0 : 1) * img.mFrameW;
            for (int r = 0; r < img.mFrameH; r++)
            {
                for (int c = 0; c < img.mFrameW; c++)
                {
                    const uint8_t p = img.mPixels[r * img.mW + sx + c];
                    if (p)
                    {
                        Canvas_Plot(x + c, y + r + kMenuViewY, p);
                    }
                }
            }
        }
    }
}

void DrawSelectors()
{
    // MainMenuState: the selector at (2, 34 + 16 * state); FileSubstate's at its label, 7px left and 2px down
    if (gHaveArrow)
    {
        Sprite_Update(gArrow);
        Sprite_Draw(gArrow, kViewX + 2, 34 + gMenuIndex * 16 + kMenuViewY);
    }
    if (gHaveSubArrow)
    {
        if (gInSub)
        {
            Sprite_Update(gSubArrow);
            Sprite_Draw(gSubArrow, kViewX + kFileX - 7, ItemY(gItem) + 2 + kMenuViewY);
        }
        else
        {
            Sprite_Hide(gSubArrow);
        }
    }
}

void EnterSub()
{
    gInSub = true;
    if (gHaveArrow)
    {
        Sprite_Play(gArrow, "disabledRight");
    }
    if (gHaveSubArrow)
    {
        Sprite_Play(gSubArrow, "enabledRight", true);
    }
}

void ExitSub()
{
    gInSub = false;
    PlaySfx("menu_select");
    if (gHaveArrow)
    {
        Sprite_Play(gArrow, "enabledRight");
    }
}

void ExitConfirmation()
{
    gItem = FileItem::NewGame;
    gConfirm = 0;
    gView = FileView::Normal;
    gMenuDirty = true;
}

void EnterCopy()
{
    gItem = gMenuIndex == 0 ? FileItem::Save2 : FileItem::Save1;
    gConfirm = 0;
    gView = FileView::CopyTo;
    gCopyMode = true;
    gMenuDirty = true;
}

void ExitCopy()
{
    gItem = FileItem::Copy;
    gConfirm = 0;
    gView = FileView::Normal;
    gCopyMode = false;
    gMenuDirty = true;
}

void CopyCheck(int target)
{
    PlaySfx("menu_select");
    gCopyTarget = target;
    if (!Save_Exists(target))
    {
        Save_Copy(gMenuIndex, target);
        ExitCopy();
    }
    else
    {
        gView = FileView::Confirm;
        gItem = FileItem::No;
        gMenuDirty = true;
    }
}

// FileSubstate.HandleInput. Returns true once a file was chosen.
bool FileInput()
{
    const bool accept = key_hit(KEY_A) || key_hit(KEY_START);
    const bool cancel = key_hit(KEY_B);
    const bool up = key_hit(KEY_UP);
    const bool down = key_hit(KEY_DOWN);
    const bool left = key_hit(KEY_LEFT);
    bool moved = false;
    bool selected = false;
    auto back = [&]() {
        if (cancel || left)
        {
            ExitSub();
        }
    };
    switch (gItem)
    {
    case FileItem::Game:
        if (accept)
        {
            PlaySfx("menu_select");
            return true;
        }
        else if (up)
        {
            moved = true;
        }
        else if (down)
        {
            moved = true;
            if (gHaveSave)
            {
                gItem = FileItem::NewGame;
            }
        }
        else
        {
            back();
        }
        break;
    case FileItem::NewGame:
        if (accept)
        {
            gItem = FileItem::No;
            selected = true;
            gView = FileView::Confirm;
            gMenuDirty = true;
        }
        else if (up)
        {
            moved = true;
            gItem = FileItem::Game;
        }
        else if (down)
        {
            moved = true;
            gItem = FileItem::Copy;
        }
        else
        {
            back();
        }
        break;
    case FileItem::Yes:
        if (accept)
        {
            selected = true;
            if (gConfirm == 2)
            {
                if (gCopyMode)
                {
                    Save_Copy(gMenuIndex, gCopyTarget);
                    ExitCopy();
                }
                else
                {
                    // DeletedSave
                    Save_Delete(gMenuIndex);
                    gHaveSave = false;
                    gItem = FileItem::Game;
                    gView = FileView::Normal;
                    gMenuDirty = true;
                }
            }
            else
            {
                gItem = FileItem::No;
                gConfirm++;
                gMenuDirty = true;
            }
        }
        else if (cancel)
        {
            selected = true;
            if (gCopyMode)
            {
                EnterCopy();
            }
            else
            {
                ExitConfirmation();
            }
        }
        else if (up)
        {
            moved = true;
            gItem = FileItem::No;
        }
        else if (down)
        {
            moved = true;
        }
        break;
    case FileItem::No:
        if (accept || cancel)
        {
            selected = true;
            if (gCopyMode)
            {
                EnterCopy();
            }
            else
            {
                ExitConfirmation();
            }
        }
        else if (up)
        {
            moved = true;
        }
        else if (down)
        {
            moved = true;
            gItem = FileItem::Yes;
        }
        break;
    case FileItem::Copy:
        if (accept)
        {
            selected = true;
            EnterCopy();
        }
        else if (up)
        {
            moved = true;
            gItem = FileItem::NewGame;
        }
        else if (down)
        {
            moved = true;
        }
        else
        {
            back();
        }
        break;
    case FileItem::Save1:
    case FileItem::Save2:
    case FileItem::Save3:
    {
        const int n = static_cast<int>(gItem) - static_cast<int>(FileItem::Save1);
        if (accept)
        {
            CopyCheck(n);
        }
        else if (cancel)
        {
            selected = true;
            ExitCopy();
        }
        else if (up)
        {
            moved = true;
            // (the file being copied is skipped)
            int m = n - 1;
            if (m == gMenuIndex)
            {
                m--;
            }
            if (m >= 0)
            {
                gItem = static_cast<FileItem>(static_cast<int>(FileItem::Save1) + m);
            }
        }
        else if (down)
        {
            moved = true;
            int m = n + 1;
            if (m == gMenuIndex)
            {
                m++;
            }
            gItem = m > 2 ? FileItem::Cancel : static_cast<FileItem>(static_cast<int>(FileItem::Save1) + m);
        }
        break;
    }
    case FileItem::Cancel:
        if (accept || cancel)
        {
            selected = true;
            ExitCopy();
        }
        else if (up)
        {
            moved = true;
            const int m = gMenuIndex == 2 ? 1 : 2;
            gItem = static_cast<FileItem>(static_cast<int>(FileItem::Save1) + m);
        }
        else if (down)
        {
            moved = true;
        }
        break;
    }
    if (moved)
    {
        PlaySfx("menu_move");
    }
    if (selected)
    {
        PlaySfx("menu_select");
    }
    return false;
}

void StartTitle()
{
    // A new TitleState: the door below the screen, the credits to come, black fading in.
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ | DCNT_OBJ_1D | DCNT_WIN0;
    HideAllSprites();
    TintTitle(0);
    gState = State::IntroFade;
    gTimer = 0;
    gPix = 256;
    gDark = 0;
    gGlint = 0;
    gBlinkTimer = 0;
    gPressVisible = true;
    gCredSet = 0;
    Canvas_Clear();
    SetDoorY(180 << 16);
    StartFlash(120, false); // Flash(2s, Black)
    ApplyFade();
}
} // namespace

void Title_Enter(ObjVramAllocator& vram)
{
    memset16(pal_bg_mem, 0, 256); // black backdrop before the layers/window are switched over
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ | DCNT_OBJ_1D | DCNT_WIN0;
    REG_BG0CNT = 0;
    REG_BG0HOFS = REG_BG0VOFS = REG_BG1HOFS = REG_BG1VOFS = 0;
    REG_BG3CNT = BG_CBB(0) | BG_SBB(kSbBg) | BG_8BPP | BG_REG_32x32 | BG_PRIO(3);
    // 32x64 (SBB 29-30): the door sits below the screen while it rises, and a 32-row map would wrap it round to the top.
    REG_BG2CNT = BG_CBB(0) | BG_SBB(kSbDoor) | BG_8BPP | BG_REG_32x64 | BG_PRIO(2);
    REG_BG1CNT = BG_CBB(kCbbText) | BG_SBB(kSbText) | BG_4BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_WIN0H = (kViewX << 8) | (kViewX + 160);
    REG_WIN0V = (0 << 8) | 160;
    REG_WININ = 0x3F;
    REG_WINOUT = 0;

    memcpy16(pal_bg_mem, title::kPalette, 176);
    pal_bg_mem[kTextBank * 16 + kInkGrey] = RGB15(15, 18, 18);  // Color(116, 140, 144)
    pal_bg_mem[kTextBank * 16 + kInkWhite] = RGB15(31, 31, 31);
    // the health boxes' colours (hud::kImgHealth uses 4 and 11-15 of the HUD's bank, as the HUD loads it)
    pal_bg_mem[kTextBank * 16 + 4] = hud::kPalette14[3];
    for (int i = 11; i < 16; i++)
    {
        pal_bg_mem[kTextBank * 16 + i] = hud::kPalette14[i - 1];
    }
    LZ77UnCompVram(title::kTilesPacked, tile_mem[0]);
    Canvas_Init();

    // Door map (SBB 29, 32x64: SBB 30 holds its lower half): the door's tiles in the top-left, the rest empty.
    memset16(se_mem[kSbDoor], 0, 32 * 64);
    for (int y = 0; y < title::kDoorTilesH; y++)
    {
        for (int x = 0; x < title::kDoorTilesW; x++)
        {
            se_mem[kSbDoor][y * 32 + x] = title::kDoorMap[y * title::kDoorTilesW + x];
        }
    }

    int oam = 0;
    // Inner rays (opaque, over the disc) take the very first slots, then the disc, then the additive outer rays,
    // which then only show outside it, as in the C#.
    gHaveInner = Sprite_Create(gInner1, vram, "door_spinglow1_inner", 12, oam);
    oam += gHaveInner ? Sprite_OamCount(gInner1) : 0;
    gHaveInner = gHaveInner && Sprite_Create(gInner2, vram, "door_spinglow2_inner", 13, oam);
    oam += gHaveInner ? Sprite_OamCount(gInner2) : 0;
    gHaveGlow = Sprite_Create(gGlow, vram, "door_glow", 7, oam);
    oam += gHaveGlow ? Sprite_OamCount(gGlow) : 0;
    gHaveSpin = Sprite_Create(gSpin1, vram, "door_spinglow1", 9, oam);
    oam += gHaveSpin ? Sprite_OamCount(gSpin1) : 0;
    gHaveSpin = gHaveSpin && Sprite_Create(gSpin2, vram, "door_spinglow2", 10, oam);
    oam += gHaveSpin ? Sprite_OamCount(gSpin2) : 0;
    gHaveText = Sprite_Create(gTitleText, vram, "title_text", 4, oam);
    oam += gHaveText ? Sprite_OamCount(gTitleText) : 0;
    gHaveRemake = Sprite_Create(gRemake, vram, "title_remake", 5, oam);
    oam += gHaveRemake ? Sprite_OamCount(gRemake) : 0;
    gHavePress = Sprite_Create(gPressEnter, vram, "press_enter", 6, oam);
    oam += gHavePress ? Sprite_OamCount(gPressEnter) : 0;
    gHaveArrow = Sprite_Create(gArrow, vram, "arrows", 8, oam);
    oam += gHaveArrow ? Sprite_OamCount(gArrow) : 0;
    gHaveSubArrow = Sprite_Create(gSubArrow, vram, "arrows", 8, oam);
    if (gHaveArrow)
    {
        Sprite_Play(gArrow, "enabledRight");
    }

    gBgScroll = 0;
    gBgTopRow = -1;
    UpdateBg(false);
    StartTitle();

    const int32_t bgm = AudioManager_FindBgm("title");
    if (bgm >= 0)
    {
        AudioManager_PlayBGM(static_cast<uint32_t>(bgm));
    }
}

bool Title_Update(int* slot)
{
    static uint32_t sFrames = 0; // (the seed: frames on the title, and where in the frame the press was read)
    sFrames++;
    const bool anyKey = key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START);
    UpdateBg(true);
    UpdateFlash();
    bool start = false;

    // Any key before the title is up: PressStart, Flash(1.5s, White), and on its first (full) frame DisplayTitle.
    const bool credits = gState == State::IntroFade || gState == State::CreditsWrite || gState == State::CreditsFade ||
                         gState == State::CreditsFadeEnd || gState == State::ScrollUp;
    if (credits && anyKey)
    {
        Canvas_Clear();
        SetDoorY(kDoorEndY << 16);
        StartFlash(90, true);
        TintTitle(65536);
        gGlint = 65536;
        gPressVisible = true;
        gBlinkTimer = 0;
        gState = State::Title;
        gTimer = 0;
        DrawTitleSprites();
        ApplyFade();
        return false;
    }

    switch (gState)
    {
    case State::IntroFade:
        if (gFlash == 0)
        {
            CreditsStart(0);
            gState = State::CreditsWrite;
            gTimer = 0;
        }
        break;
    case State::CreditsWrite:
        // TextDrawTimer: a character every 0.03s, which at 60fps comes to every other frame
        gTimer++;
        if ((gTimer & 1) == 0 && gCredRemaining > 0)
        {
            gCredRng = gCredRng * 1664525u + 1013904223u;
            const int pick = static_cast<int>((gCredRng >> 8) % static_cast<uint32_t>(gCredRemaining));
            const uint8_t line = gCredOrder[pick][0];
            const uint8_t col = gCredOrder[pick][1];
            Canvas_Char(title::kCreditLines[gCredSet * 3 + line][col], gCredX[line] + col * kCharW, gCredY[line],
                        kInkCredits, kViewY);
            gCredOrder[pick][0] = gCredOrder[gCredRemaining - 1][0];
            gCredOrder[pick][1] = gCredOrder[gCredRemaining - 1][1];
            gCredRemaining--;
        }
        if (gCredRemaining == 0)
        {
            gState = State::CreditsFade;
        }
        break;
    case State::CreditsFade:
        gCredOpacity -= 65536 * 6 / 600; // 0.6/s down to 0.3
        if (gCredOpacity <= 65536 * 3 / 10)
        {
            gCredOpacity = 65536 * 3 / 10;
            gState = State::CreditsFadeEnd;
            gTimer = 0;
        }
        SetCreditsOpacity(gCredOpacity);
        break;
    case State::CreditsFadeEnd:
        gCredOpacity -= 65536 * 2 / 60; // then to 0 at 2/s, for 1s (TextFadeTimer)
        if (gCredOpacity < 0)
        {
            gCredOpacity = 0;
        }
        SetCreditsOpacity(gCredOpacity);
        gTimer++;
        if (gTimer >= 60)
        {
            if (gCredSet == 0)
            {
                CreditsStart(1);
                gState = State::CreditsWrite;
            }
            else
            {
                Canvas_Clear();
                gState = State::ScrollUp;
            }
            gTimer = 0;
        }
        break;
    case State::ScrollUp:
        SetDoorY(gDoorY - kDoorRise);
        if (DoorY() <= kDoorEndY)
        {
            SetDoorY(kDoorEndY << 16);
            StartFlash(90, true);
            TintTitle(65536);
            gGlint = 65536;
            gPressVisible = true;
            gBlinkTimer = 0;
            gState = State::Title;
            gTimer = 0;
            DrawTitleSprites();
        }
        break;
    case State::Title:
    {
        // DisplayTitle: the white glint fades at 0.4/s, "press enter" blinks every second
        gGlint = gGlint > 65536 * 4 / 600 ? gGlint - 65536 * 4 / 600 : 0;
        TintTitle(gGlint);
        if (++gBlinkTimer >= kBlinkFrames)
        {
            gBlinkTimer = 0;
            gPressVisible = !gPressVisible;
        }
        DrawTitleSprites();
        if (anyKey)
        {
            PlaySfx("menu_select");
            gState = State::Pixelate;
            gPix = 256;
            gDark = 0;
            gTimer = 0;
        }
        break;
    }
    case State::Pixelate:
    case State::FadeOut:
    {
        // TitleState Pixelate/FadeOut: pixelation +15 block-px/s throughout; the black overlay +0.36/s until the
        // pixelation reaches 15, then +0.72/s until fully black. (The glint and the blink stop where they are.)
        DrawTitleSprites();
        gTimer++;
        gPix += 15 * 256 / 60;
        gDark += (gState == State::Pixelate ? 36 : 72) * 65536 / 100 / 60;
        if (gState == State::Pixelate && gPix >= 15 * 256)
        {
            gState = State::FadeOut;
        }
        if (gDark > 65536)
        {
            gDark = 65536;
        }
        MosaicBgOffsets offsets;
        offsets.x[3] = static_cast<int16_t>(-kViewX); // BG3 background, BG2 door (see UpdateBg / SetDoorY)
        offsets.y[3] = static_cast<int16_t>((gBgScroll >> 8) - kViewY);
        offsets.x[2] = static_cast<int16_t>(-kViewX);
        offsets.y[2] = static_cast<int16_t>(-(DoorY() + kViewY));
        offsets.originY = kViewY; // the title draws on the C# screen directly, no HUD header
        Mosaic_Apply(gPix, offsets);
        if (gState == State::FadeOut && gDark >= 65536)
        {
            // MainMenuState, at once: black overlay, pixelation and flash off; only the labels and selectors.
            Mosaic_End();
            REG_DISPCNT = DCNT_MODE0 | DCNT_BG1 | DCNT_OBJ | DCNT_OBJ_1D | DCNT_WIN0;
            HideAllSprites();
            gDark = 0;
            gFlash = 0;
            gInSub = false;
            if (gHaveArrow)
            {
                Sprite_Play(gArrow, "enabledRight");
            }
            LoadFile();
            gState = State::Menu;
            gTimer = 0;
            DrawMenu();
            DrawSelectors();
        }
        break;
    }
    case State::Menu:
    {
        if (gInSub)
        {
            if (FileInput())
            {
                Rng_SeedFromPlayer(sFrames * 2654435761u ^ REG_VCOUNT);
                *slot = gMenuIndex;
                gState = State::LoadFade;
                gDark = 0;
            }
        }
        else if (key_hit(KEY_B))
        {
            PlaySfx("pause_sound");
            StartTitle();
            break;
        }
        else if (key_hit(KEY_A) || key_hit(KEY_START) || key_hit(KEY_RIGHT))
        {
            PlaySfx("menu_select");
            if (gMenuIndex < 3)
            {
                EnterSub();
            }
        }
        else if (key_hit(KEY_UP))
        {
            PlaySfx("menu_move");
            if (gMenuIndex > 0)
            {
                gMenuIndex--;
                LoadFile();
            }
        }
        else if (key_hit(KEY_DOWN))
        {
            PlaySfx("menu_move");
            if (gMenuIndex < kMenuItems - 1)
            {
                gMenuIndex++;
                LoadFile();
            }
        }
        DrawMenu();
        DrawSelectors();
        break;
    }
    case State::LoadFade:
        gDark += 72 * 65536 / 100 / 60;
        DrawMenu();
        DrawSelectors();
        if (gDark >= 65536)
        {
            gDark = 65536;
            start = true;
        }
        break;
    }
    ApplyFade();
    return start;
}

void Title_Exit(ObjVramAllocator& vram)
{
    Sprite* all[] = { &gTitleText, &gRemake, &gPressEnter, &gGlow, &gArrow, &gSubArrow, &gInner1, &gInner2, &gSpin1, &gSpin2 };
    for (Sprite* s : all)
    {
        Sprite_Destroy(*s, vram);
    }
    gHaveText = gHaveRemake = gHavePress = gHaveGlow = gHaveArrow = gHaveSubArrow = gHaveSpin = gHaveInner = false;
    REG_DISPCNT |= DCNT_BG3;
    REG_BLDCNT = 0;
    REG_BLDY = 0;
    REG_BG2HOFS = REG_BG2VOFS = REG_BG3HOFS = REG_BG3VOFS = 0;
    REG_DISPCNT |= DCNT_BG2;
}
