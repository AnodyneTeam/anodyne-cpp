#include "title.h"

#include <string.h>

#include "tonc.h"

#include "audio_manager.h"
#include "sprite.h"
#include "../build/tools/title_data.h"

namespace
{
constexpr int kViewX = 40;       // the 160px playfield is centred on the 240px screen
constexpr int kViewY = -10;      // the C# screen is 180px tall; show rows 10..170
constexpr int kSbBg = 28;
constexpr int kSbDoor = 29;
constexpr int kSbText = 27;
constexpr int kCbbText = 3;
constexpr int kBgScrollSpeed = 30;     // px/s (ScrollingTex velocity (0, -30))
constexpr int kDoorRiseSpeed = 20;     // px/s
constexpr int kDoorEndY = 180 - 116;   // door top when fully risen
constexpr int kFlashFrames = 45;       // 1.5s
constexpr int kFadeFrames = 30;
constexpr int kBlinkFrames = 60;
constexpr int kMenuItems = 4;

enum class State
{
    Rise,      // door scrolls up
    Flash,     // white flash into the title
    Title,     // title text + blinking "press start"
    FadeOut,   // to black, then the menu
    MenuFadeIn,
    Menu
};

State gState = State::Rise;
int gTimer = 0;
int gBgScroll = 0;       // Fx 24.8, px scrolled
int gBgTopRow = -1;
int gDoorY = 180;        // C# coordinates
int gDoorSub = 0;        // Fx 8 fraction
int gMenuIndex = 0;
int gBlink = 0;

Sprite gTitleText;
Sprite gRemake;
Sprite gPressEnter;
Sprite gGlow;
Sprite gArrow;
bool gHaveText = false;
bool gHaveRemake = false;
bool gHavePress = false;
bool gHaveGlow = false;
bool gHaveArrow = false;
bool gShowTitleSprites = false;

void PlaySfx(const char* name)
{
    const int32_t id = AudioManager_FindSfx(name);
    if (id >= 0)
    {
        AudioManager_PlaySFX(static_cast<uint32_t>(id));
    }
}

void Fade(int mode, int level)
{
    // mode: 0 none, 2 lighten, 3 darken (BLDCNT effect bits 6-7); applies to everything.
    REG_BLDCNT = (mode << 6) | 0x3F;
    REG_BLDY = static_cast<uint16_t>(level);
}

void ClearText()
{
    for (int i = 0; i < 32 * 32; i++)
    {
        se_mem[kSbText][i] = 0;
    }
}

// 8x8 glyph cells on BG1, palette bank `bank` (14/15 colour index 1).
void PutText(int tx, int ty, const char* text, int bank)
{
    for (int i = 0; text[i] && tx + i < 32; i++)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const uint8_t glyph = c < 128 ? title::kFontAscii[c] : 0;
        se_mem[kSbText][ty * 32 + tx + i] = static_cast<uint16_t>(c == ' ' ? 0 : (glyph + 1) | (bank << 12));
    }
}

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
        // Rows top..top+21 must be present; fill any that are new.
        const int first = (gBgTopRow < 0) ? top : gBgTopRow + 22;
        const int last = top + 21;
        for (int r = (first > last - 31 ? first : last - 31); r <= last; r++)
        {
            WriteBgRow(r);
        }
        gBgTopRow = top;
    }
    REG_BG3HOFS = static_cast<uint16_t>(-kViewX);
    REG_BG3VOFS = static_cast<uint16_t>(px - kViewY);
}

void SetDoorY(int y)
{
    gDoorY = y;
    REG_BG2HOFS = static_cast<uint16_t>(-kViewX);
    REG_BG2VOFS = static_cast<uint16_t>(-(y + kViewY));
}

void HideAllSprites()
{
    if (gHaveText) { Sprite_Hide(gTitleText); }
    if (gHaveRemake) { Sprite_Hide(gRemake); }
    if (gHavePress) { Sprite_Hide(gPressEnter); }
    if (gHaveGlow) { Sprite_Hide(gGlow); }
    if (gHaveArrow) { Sprite_Hide(gArrow); }
}

void DrawTitleSprites(bool pressVisible)
{
    if (gHaveText) { Sprite_Draw(gTitleText, kViewX + 16, 16 + kViewY); }
    if (gHaveRemake) { Sprite_Draw(gRemake, kViewX + 45, 47 + kViewY); }
    if (gHaveGlow) { Sprite_Draw(gGlow, kViewX + 48, kDoorEndY + 17 + kViewY); }
    if (gHavePress)
    {
        if (pressVisible)
        {
            Sprite_Draw(gPressEnter, kViewX + 32, 150 + kViewY);
        }
        else
        {
            Sprite_Hide(gPressEnter);
        }
    }
}

void DrawMenu()
{
    ClearText();
    static const char* const kLabels[kMenuItems] = { "1", "2", "3", "Config" };
    for (int i = 0; i < kMenuItems; i++)
    {
        PutText(kViewX / 8 + 2, 5 + i * 2, kLabels[i], i == gMenuIndex ? 15 : 14);
    }
    if (gMenuIndex < 3)
    {
        PutText(kViewX / 8 + 6, 5 + gMenuIndex * 2, "New Game", 15);
    }
    PutText(kViewX / 8 + 1, 17, "A: select  B: back", 14);
    if (gHaveArrow)
    {
        Sprite_Draw(gArrow, kViewX + 2, 34 + kViewY + 14 + gMenuIndex * 16 - 6 + 0);
    }
}

void EnterTitleLayout()
{
    SetDoorY(kDoorEndY);
    gShowTitleSprites = true;
    ClearText();
    HideAllSprites();
    gBlink = 0;
}
} // namespace

void Title_Enter(ObjVramAllocator& vram)
{
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ | DCNT_OBJ_1D | DCNT_WIN0;
    REG_BG0CNT = 0;
    REG_BG0HOFS = REG_BG0VOFS = REG_BG1HOFS = REG_BG1VOFS = 0;
    REG_BG3CNT = BG_CBB(0) | BG_SBB(kSbBg) | BG_8BPP | BG_REG_32x32 | BG_PRIO(3);
    REG_BG2CNT = BG_CBB(0) | BG_SBB(kSbDoor) | BG_8BPP | BG_REG_32x32 | BG_PRIO(2);
    REG_BG1CNT = BG_CBB(kCbbText) | BG_SBB(kSbText) | BG_4BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_WIN0H = (kViewX << 8) | (kViewX + 160);
    REG_WIN0V = (0 << 8) | 160;
    REG_WININ = 0x3F;
    REG_WINOUT = 0;

    memcpy16(pal_bg_mem, title::kPalette, 224);
    // Text banks: colour 1 of bank 14 = dim blue-grey (unselected), bank 15 = white (selected).
    memset16(&pal_bg_mem[224], 0, 32);
    pal_bg_mem[224 + 1] = RGB15(14, 17, 18);
    pal_bg_mem[240 + 1] = RGB15(31, 31, 31);
    LZ77UnCompVram(title::kTilesPacked, tile_mem[0]);
    // Glyph tiles start at tile 1 (tile 0 stays blank).
    memset32(&tile_mem[kCbbText][0], 0, 8);
    memcpy32(&tile_mem[kCbbText][1], title::kFontTiles, sizeof(title::kFontTiles) / 4);

    // Door map (SBB 29): the door's tiles in the top-left, the rest empty.
    for (int i = 0; i < 32 * 32; i++)
    {
        se_mem[kSbDoor][i] = 0;
    }
    for (int y = 0; y < title::kDoorTilesH; y++)
    {
        for (int x = 0; x < title::kDoorTilesW; x++)
        {
            se_mem[kSbDoor][y * 32 + x] = title::kDoorMap[y * title::kDoorTilesW + x];
        }
    }
    ClearText();

    int oam = 0;
    gHaveText = Sprite_Create(gTitleText, vram, "title_text", 0, oam);
    oam += gHaveText ? Sprite_OamCount(gTitleText) : 0;
    gHaveRemake = Sprite_Create(gRemake, vram, "title_remake", 1, oam);
    oam += gHaveRemake ? Sprite_OamCount(gRemake) : 0;
    gHavePress = Sprite_Create(gPressEnter, vram, "press_enter", 2, oam);
    oam += gHavePress ? Sprite_OamCount(gPressEnter) : 0;
    gHaveGlow = Sprite_Create(gGlow, vram, "door_glow", 3, oam);
    oam += gHaveGlow ? Sprite_OamCount(gGlow) : 0;
    gHaveArrow = Sprite_Create(gArrow, vram, "arrows", 4, oam);
    if (gHaveArrow)
    {
        Sprite_Play(gArrow, "enabledRight");
    }
    HideAllSprites();

    gState = State::Rise;
    gTimer = 0;
    gBgScroll = 0;
    gBgTopRow = -1;
    gMenuIndex = 0;
    gShowTitleSprites = false;
    SetDoorY(180);
    UpdateBg(false);
    Fade(3, 16);

    const int32_t bgm = AudioManager_FindBgm("title");
    if (bgm >= 0)
    {
        AudioManager_PlayBGM(static_cast<uint32_t>(bgm));
    }
}

bool Title_Update(int* slot)
{
    const bool anyKey = key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START);
    UpdateBg(true);
    bool start = false;

    switch (gState)
    {
    case State::Rise:
    {
        // Fade in from black over the first second while the door climbs.
        if (gTimer < 60)
        {
            Fade(3, 16 - (gTimer * 16) / 60);
        }
        else
        {
            Fade(0, 0);
        }
        gDoorSub += kDoorRiseSpeed * 256 / 60;
        gDoorY -= gDoorSub >> 8;
        gDoorSub &= 255;
        if (gDoorY <= kDoorEndY)
        {
            gDoorY = kDoorEndY;
        }
        SetDoorY(gDoorY);
        gTimer++;
        if (anyKey || gDoorY <= kDoorEndY)
        {
            gState = State::Flash;
            gTimer = 0;
        }
        break;
    }
    case State::Flash:
    {
        // Up to white, swap in the title layout at the peak, back down.
        const int half = kFlashFrames / 2;
        if (gTimer < half)
        {
            Fade(2, (gTimer * 16) / half);
        }
        else
        {
            if (gTimer == half)
            {
                EnterTitleLayout();
            }
            Fade(2, 16 - ((gTimer - half) * 16) / half);
        }
        if (gShowTitleSprites)
        {
            DrawTitleSprites(false);
        }
        gTimer++;
        if (gTimer >= kFlashFrames)
        {
            Fade(0, 0);
            gState = State::Title;
            gTimer = 0;
        }
        break;
    }
    case State::Title:
    {
        gBlink = (gTimer / kBlinkFrames) & 1;
        DrawTitleSprites(!gBlink);
        gTimer++;
        if (anyKey)
        {
            PlaySfx("menu_select");
            gState = State::FadeOut;
            gTimer = 0;
        }
        break;
    }
    case State::FadeOut:
    {
        Fade(3, (gTimer * 16) / kFadeFrames);
        gTimer++;
        if (gTimer >= kFadeFrames)
        {
            // The menu: background only (door and title art hidden), labels on BG1.
            REG_DISPCNT &= ~DCNT_BG2;
            HideAllSprites();
            gShowTitleSprites = false;
            gState = State::MenuFadeIn;
            gTimer = 0;
            DrawMenu();
        }
        break;
    }
    case State::MenuFadeIn:
    {
        Fade(3, 16 - (gTimer * 16) / kFadeFrames);
        gTimer++;
        if (gTimer >= kFadeFrames)
        {
            Fade(0, 0);
            gState = State::Menu;
        }
        Sprite_Update(gArrow);
        DrawMenu();
        break;
    }
    case State::Menu:
    {
        if (key_hit(KEY_UP) && gMenuIndex > 0)
        {
            gMenuIndex--;
            PlaySfx("menu_move");
        }
        else if (key_hit(KEY_DOWN) && gMenuIndex < kMenuItems - 1)
        {
            gMenuIndex++;
            PlaySfx("menu_move");
        }
        else if (key_hit(KEY_A) || key_hit(KEY_START))
        {
            PlaySfx("menu_select");
            if (gMenuIndex < 3)
            {
                *slot = gMenuIndex;
                start = true;
            }
        }
        else if (key_hit(KEY_B))
        {
            PlaySfx("pause_sound");
            REG_DISPCNT |= DCNT_BG2;
            EnterTitleLayout();
            gState = State::Title;
            gTimer = 0;
        }
        if (gHaveArrow)
        {
            Sprite_Update(gArrow);
        }
        DrawMenu();
        break;
    }
    }
    return start;
}

void Title_Exit(ObjVramAllocator& vram)
{
    Sprite* all[] = { &gTitleText, &gRemake, &gPressEnter, &gGlow, &gArrow };
    for (Sprite* s : all)
    {
        Sprite_Destroy(*s, vram);
    }
    gHaveText = gHaveRemake = gHavePress = gHaveGlow = gHaveArrow = false;
    REG_BLDCNT = 0;
    REG_BLDY = 0;
    REG_BG2HOFS = REG_BG2VOFS = REG_BG3HOFS = REG_BG3VOFS = 0;
    REG_DISPCNT |= DCNT_BG2;
}
