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
// Affine matrix slot n lives in the attr3 words of OAM entries 4n..4n+3, so use the last slots (OAM 120-127 unused).
constexpr int kSpinAffine1 = 30;
constexpr int kSpinAffine2 = 31;

// Dialogue box (C# TextBox, bottom position): sprite at (2, 114), text cells on the 8px BG1 grid inside it.
constexpr int kBoxX = kViewX + 2;
constexpr int kBoxY = 160 - 44 - 2;
constexpr int kTextCol = (kViewX + 8) / 8;
constexpr int kTextRow = (kBoxY + 6) / 8;
constexpr int kTextCols = 18;
constexpr int kTextLines = 4;
constexpr int kCharsPerFrame = 2; // IntroState: DialogueState speed x4

enum class State
{
    Rise,      // door scrolls up
    Flash,     // white flash into the title
    Title,     // title text + blinking "press start"
    FadeOut,   // to black, then the menu
    MenuFadeIn,
    Menu,
    IntroFade,  // menu -> black
    IntroBox,   // the sage's dialogue
    IntroWait   // 1.5s of black, then the game
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
Sprite gSpin1;
Sprite gSpin2;
Sprite gBox;
Sprite gBlinky;
bool gHaveText = false;
bool gHaveRemake = false;
bool gHavePress = false;
bool gHaveGlow = false;
bool gHaveArrow = false;
bool gHaveSpin = false;
bool gHaveBox = false;
bool gHaveBlinky = false;
int gSpinAngle = 0;
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
    if (gHaveSpin) { Sprite_Hide(gSpin1); Sprite_Hide(gSpin2); }
    if (gHaveBox) { Sprite_Hide(gBox); }
    if (gHaveBlinky) { Sprite_Hide(gBlinky); }
}

void DrawTitleSprites(bool pressVisible)
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
        Sprite_DrawAffine(gSpin1, kViewX + 80, kDoorEndY + 32 + kViewY, kSpinAffine1);
        Sprite_DrawAffine(gSpin2, kViewX + 80, kDoorEndY + 32 + kViewY, kSpinAffine2);
    }
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

// ---- new-game intro dialogue ----
char gLines[48][kTextCols + 1];
int gLineCount = 0;
int gPage = 0;
int gBoxFirstLine = 0;
int gReveal = 0;
int gBoxChars = 0;

void LoadPage()
{
    // Greedy word wrap of the page into kTextCols columns; '\n' forces a break.
    gLineCount = 0;
    const char* text = title::kIntroPages[gPage];
    int col = 0;
    memset(gLines, 0, sizeof(gLines));
    const char* p = text;
    while (*p && gLineCount < 47)
    {
        if (*p == '\n')
        {
            gLineCount++;
            col = 0;
            p++;
            continue;
        }
        if (*p == ' ')
        {
            if (col > 0)
            {
                gLines[gLineCount][col++] = ' ';
            }
            p++;
            continue;
        }
        int wordLen = 0;
        while (p[wordLen] && p[wordLen] != ' ' && p[wordLen] != '\n')
        {
            wordLen++;
        }
        if (col > 0 && col + wordLen > kTextCols)
        {
            while (col > 0 && gLines[gLineCount][col - 1] == ' ')
            {
                gLines[gLineCount][--col] = 0;
            }
            gLineCount++;
            col = 0;
        }
        for (int i = 0; i < wordLen && col < kTextCols; i++)
        {
            gLines[gLineCount][col++] = p[i];
        }
        p += wordLen;
    }
    gLineCount++;
    gBoxFirstLine = 0;
}

void StartBox()
{
    gReveal = 0;
    gBoxChars = 0;
    for (int l = gBoxFirstLine; l < gLineCount && l < gBoxFirstLine + kTextLines; l++)
    {
        gBoxChars += static_cast<int>(strlen(gLines[l]));
    }
    ClearText();
}

void DrawBoxText()
{
    int left = gReveal;
    for (int l = 0; l < kTextLines && gBoxFirstLine + l < gLineCount; l++)
    {
        const char* line = gLines[gBoxFirstLine + l];
        const int len = static_cast<int>(strlen(line));
        char buf[kTextCols + 1];
        const int n = left < len ? left : len;
        memcpy(buf, line, n);
        buf[n] = 0;
        PutText(kTextCol, kTextRow + l, buf, 15);
        left -= n;
        if (left <= 0)
        {
            break;
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
    oam += gHaveArrow ? Sprite_OamCount(gArrow) : 0;
    gHaveSpin = Sprite_Create(gSpin1, vram, "door_spinglow1", 5, oam);
    oam += gHaveSpin ? Sprite_OamCount(gSpin1) : 0;
    gHaveSpin = gHaveSpin && Sprite_Create(gSpin2, vram, "door_spinglow2", 6, oam);
    oam += gHaveSpin ? Sprite_OamCount(gSpin2) : 0;
    gHaveBox = Sprite_Create(gBox, vram, "dialogue_box", 7, oam);
    oam += gHaveBox ? Sprite_OamCount(gBox) : 0;
    gHaveBlinky = Sprite_Create(gBlinky, vram, "dialogue_blinky_box", 8, oam);
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
                gState = State::IntroFade;
                gTimer = 0;
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
    case State::IntroFade:
    {
        Fade(3, (gTimer * 16) / kFadeFrames);
        gTimer++;
        if (gTimer >= kFadeFrames)
        {
            REG_DISPCNT &= ~(DCNT_BG2 | DCNT_BG3);
            HideAllSprites();
            ClearText();
            const int32_t bgm = AudioManager_FindBgm("blank");
            if (bgm >= 0)
            {
                AudioManager_PlayBGM(static_cast<uint32_t>(bgm));
            }
            Fade(0, 0);
            gPage = 0;
            LoadPage();
            StartBox();
            gState = State::IntroBox;
            gTimer = 0;
        }
        break;
    }
    case State::IntroBox:
    {
        const bool full = gReveal >= gBoxChars;
        if (!full)
        {
            gReveal += kCharsPerFrame;
            if (key_hit(KEY_A) || key_hit(KEY_B))
            {
                gReveal = gBoxChars;
            }
            if (gReveal > gBoxChars)
            {
                gReveal = gBoxChars;
            }
        }
        DrawBoxText();
        if (gHaveBox)
        {
            Sprite_Draw(gBox, kBoxX, kBoxY);
        }
        if (full)
        {
            gTimer++;
            if (gHaveBlinky)
            {
                if ((gTimer / 24) & 1)
                {
                    Sprite_Hide(gBlinky);
                }
                else
                {
                    Sprite_Draw(gBlinky, kBoxX + 156 - 10, kBoxY + 44 - 10);
                }
            }
            if (key_hit(KEY_A) || key_hit(KEY_B))
            {
                gTimer = 0;
                gBoxFirstLine += kTextLines;
                if (gBoxFirstLine >= gLineCount)
                {
                    gPage++;
                    if (gPage >= title::kIntroPageCount)
                    {
                        HideAllSprites();
                        ClearText();
                        gState = State::IntroWait;
                        break;
                    }
                    LoadPage();
                }
                StartBox();
            }
        }
        break;
    }
    case State::IntroWait:
    {
        gTimer++;
        if (gTimer >= 90)
        {
            start = true;
        }
        break;
    }
    }
    return start;
}

void Title_Exit(ObjVramAllocator& vram)
{
    Sprite* all[] = { &gTitleText, &gRemake, &gPressEnter, &gGlow, &gArrow, &gSpin1, &gSpin2, &gBox, &gBlinky };
    for (Sprite* s : all)
    {
        Sprite_Destroy(*s, vram);
    }
    gHaveText = gHaveRemake = gHavePress = gHaveGlow = gHaveArrow = gHaveSpin = gHaveBox = gHaveBlinky = false;
    REG_DISPCNT |= DCNT_BG3;
    REG_BLDCNT = 0;
    REG_BLDY = 0;
    REG_BG2HOFS = REG_BG2VOFS = REG_BG3HOFS = REG_BG3VOFS = 0;
    REG_DISPCNT |= DCNT_BG2;
}
