#include "dialog.h"

#include <string.h>

#include "tonc.h"

#include "audio_manager.h"
#include "dialog_data.h"
#include "title_data.h"

// Read by the headless test driver (src/tools/bot/gba_drive.py).
volatile int gDialogueOpen = 0;

namespace
{
// ---- Scene lookup (DialogueManager / DialogueScene) ---------------------------------------------------------------
const char* gArea = "";
uint8_t gSceneLine[kDialogSceneCount > 0 ? kDialogSceneCount : 1] __attribute__((section(".sbss")));
uint8_t gSceneFinished[kDialogSceneCount > 0 ? kDialogSceneCount : 1] __attribute__((section(".sbss")));

const DialogSceneData* FindScene(const char* npc, const char* area, const char* scene, unsigned* index)
{
    for (unsigned i = 0; i < kDialogSceneCount; i++)
    {
        const DialogSceneData& s = kDialogScenes[i];
        if (strcmp(s.mScene, scene) == 0 && strcmp(s.mNpc, npc) == 0 && strcmp(s.mArea, area) == 0)
        {
            *index = i;
            return &s;
        }
    }
    return nullptr;
}

// ---- Text box geometry (TextBox.cs) -------------------------------------------------------------------------------
constexpr int kBoxW = 156;
constexpr int kBoxH = 44;
constexpr int kBoxX = 40 + 2;    // playfield starts at x=40 on the GBA screen; the box is 2px in
constexpr int kBoxYDown = 160 - kBoxH - 2;
constexpr int kBoxYTop = 2;
constexpr int kTextX = 4;        // Writer area: box + (4, 5) and TextWriter.SetWriteArea adds 2 to y
constexpr int kTextY = 7;
constexpr int kTextWidth = kBoxW - 16;
constexpr int kLineSep = 10;     // font line height 8 + 2
constexpr int kCharW = 7;
constexpr int kLinesPerBox = 3;
constexpr int kMaxLines = 5;
constexpr int kMaxLineChars = 24;

// OBJ layout of the 160x48 canvas: three 64x32 OBJs across the top, five 32x16 OBJs along the bottom.
constexpr int kFirstOam = 120;
constexpr int kObjCount = 8;
constexpr int kCanvasTiles = 3 * 32 + 5 * 8;
constexpr int kPalBank = 15;

// Palette indices.
constexpr uint32_t kFill = 1;
constexpr uint32_t kBlack = 2;
constexpr uint32_t kWhite = 3;
constexpr uint32_t kRedLight = 4;
constexpr uint32_t kRedDark = 5;
constexpr uint32_t kPromptRed = 6;

ObjVramAllocator* gVram = nullptr;
int gObjBlock[kObjCount];
bool gOpen = false;
bool gLabels = false; // Dialog_ShowLabels holds the OBJs
bool gTop = false;
bool gIntro = false;

alignas(4) uint32_t gCanvas[kCanvasTiles * 8] __attribute__((section(".sbss")));  // 4bpp tiles, in OBJ order (each OBJ's tiles consecutive)
alignas(4) uint32_t gBoxImage[kCanvasTiles * 8] __attribute__((section(".sbss"))); // the empty box, for fast redraws
bool gUploadNeeded = false;

uint16_t gSavedBldCnt = 0;
uint16_t gSavedBldAlpha = 0;

int32_t gSfxBlip = -1;
int32_t gSfxBloop = -1;

constexpr int TileIndex(int tx, int ty)
{
    return ty < 4 ? (tx / 8) * 32 + ty * 8 + (tx & 7) : 96 + (tx / 4 - 0) * 8 + (ty - 4) * 4 + (tx & 3);
}

uint32_t* TileWord(int x, int y)
{
    return &gCanvas[TileIndex(x >> 3, y >> 3) * 8 + (y & 7)];
}

void SetPixel(uint32_t* buf, int x, int y, uint32_t c)
{
    if (x < 0 || x >= 160 || y < 0 || y >= 48)
    {
        return;
    }
    uint32_t* w = &buf[TileIndex(x >> 3, y >> 3) * 8 + (y & 7)];
    const int sh = (x & 7) * 4;
    *w = (*w & ~(0xFu << sh)) | (c << sh);
}

// ---- Box image ----------------------------------------------------------------------------------------------------
void BuildBoxImage()
{
    memset(gBoxImage, 0, sizeof(gBoxImage));
    for (int y = 0; y < kBoxH; y++)
    {
        for (int x = 0; x < kBoxW; x++)
        {
            const bool corner = (x == 0 || x == kBoxW - 1) && (y == 0 || y == kBoxH - 1);
            if (corner)
            {
                continue;
            }
            const bool nearCorner = (x == 1 || x == kBoxW - 2) && (y == 1 || y == kBoxH - 2);
            const bool border = x == 0 || x == kBoxW - 1 || y == 0 || y == kBoxH - 1 || nearCorner;
            SetPixel(gBoxImage, x, y, border ? kBlack : kFill);
        }
    }
}

// ---- Glyphs -------------------------------------------------------------------------------------------------------
const char kFontOrder[] = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ'1234567890.:,;'\"(!?)+-*/=$[]_";

bool HasGlyph(char c)
{
    return c != 0 && strchr(kFontOrder, c) != nullptr;
}

// Draws one glyph (ink index `ink`) with its top-left at (gx, gy) of the canvas.
void DrawGlyph(char c, int gx, int gy, uint32_t ink, bool shadow)
{
    const uint32_t* rows = reinterpret_cast<const uint32_t*>(title::kFontTiles) + title::kFontAscii[static_cast<uint8_t>(c)] * 8;
    for (int pass = shadow ? 0 : 1; pass < 2; pass++)
    {
        const uint32_t color = pass == 0 ? kBlack : ink;
        for (int r = 0; r < 8; r++)
        {
            const int y = gy + r + (pass == 0 ? 1 : 0);
            if (y < 0 || y >= kBoxH)
            {
                continue;
            }
            const uint32_t w = rows[r];
            if (!w)
            {
                continue;
            }
            const int tx = gx >> 3;
            const int sh = (gx & 7) * 4;
            const uint32_t low = w << sh;
            if (tx >= 0 && tx < 20)
            {
                uint32_t* t = TileWord(tx << 3, y);
                *t = (*t & ~(low * 15u)) | (low * color);
            }
            if (sh && tx + 1 < 20)
            {
                const uint32_t high = w >> (32 - sh);
                if (high)
                {
                    uint32_t* t = TileWord((tx + 1) << 3, y);
                    *t = (*t & ~(high * 15u)) | (high * color);
                }
            }
        }
    }
}

// ---- Writer (TextWriter.cs) ---------------------------------------------------------------------------------------
struct Writer final
{
    const char* mText = "";
    int mLen = 0;
    int mProgress = 0;
    int mLine = 0;
    int mCursor = 0;
    bool mNewWord = true;
    int mSpeed = 30;          // chars per second
    int mStep = 0;            // 16.16 steps accumulated
    int mFirstLineY = 0;
    bool mJustWrote = false;
    int mLineCount = 0;
    char mLines[kMaxLines][kMaxLineChars];
    uint8_t mLineLen[kMaxLines];
    uint8_t mLineX[kMaxLines][kMaxLineChars];
} gW __attribute__((section(".sbss")));

bool AtEndOfText()
{
    return gW.mProgress >= gW.mLen;
}

bool AtEndOfBox()
{
    return gW.mLine >= kLinesPerBox;
}

char NextChar()
{
    return gW.mProgress < gW.mLen ? gW.mText[gW.mProgress] : 0;
}

void ResetWriter(const char* text)
{
    gW.mText = text;
    gW.mLen = static_cast<int>(strlen(text));
    gW.mProgress = 0;
    gW.mLine = 0;
    gW.mCursor = 0;
    gW.mNewWord = true;
    gW.mStep = 0;
    gW.mFirstLineY = 0;
    gW.mLineCount = 0;
    memset(gW.mLineLen, 0, sizeof(gW.mLineLen));
}

void RedrawText()
{
    memcpy(gCanvas, gBoxImage, sizeof(gCanvas));
    for (int l = 0; l < gW.mLineCount; l++)
    {
        const int y = kTextY + gW.mFirstLineY + l * kLineSep;
        for (int i = 0; i < gW.mLineLen[l]; i++)
        {
            const char c = gW.mLines[l][i];
            if (c != ' ')
            {
                DrawGlyph(c, kTextX + gW.mLineX[l][i], y, kWhite, true);
            }
        }
    }
    gUploadNeeded = true;
}

int WordLength()
{
    int n = 0;
    for (int i = gW.mProgress; i < gW.mLen; i++)
    {
        const char c = gW.mText[i];
        if (c == ' ' || c == '\n')
        {
            break;
        }
        n++;
    }
    return n * kCharW;
}

void NewLine()
{
    gW.mCursor = 0;
    gW.mLine++;
    gW.mProgress++;
    if (gW.mProgress < gW.mLen && gW.mText[gW.mProgress] == ' ')
    {
        gW.mProgress++;
    }
}

// TextWriter.KeepInBounds: true if the character can be placed on the current line (else the line advances).
bool KeepInBounds()
{
    if (gW.mLineLen[gW.mLine] == 0)
    {
        return true;
    }
    bool nextLine;
    if (gW.mNewWord)
    {
        const int wordLen = WordLength();
        nextLine = wordLen > kTextWidth ? gW.mCursor + kCharW > kTextWidth : gW.mCursor + wordLen > kTextWidth;
        gW.mNewWord = false;
    }
    else
    {
        nextLine = gW.mCursor + kCharW > kTextWidth;
    }
    if (nextLine)
    {
        gW.mCursor = 0;
        gW.mLine++;
        return !AtEndOfBox();
    }
    if (gW.mProgress > 0)
    {
        gW.mCursor += kCharW;
    }
    return true;
}

// One TextWriter.Write step.
void WriteStep()
{
    const char c = gW.mText[gW.mProgress];
    while (gW.mLineCount - 1 < gW.mLine && gW.mLineCount < kMaxLines)
    {
        gW.mLineLen[gW.mLineCount++] = 0;
    }
    if (gW.mLine >= kMaxLines)
    {
        gW.mProgress++;
        return;
    }
    if (c == ' ')
    {
        gW.mNewWord = true;
        gW.mCursor += kCharW;
        gW.mProgress++;
        if (gW.mLineLen[gW.mLine] < kMaxLineChars)
        {
            gW.mLines[gW.mLine][gW.mLineLen[gW.mLine]] = ' ';
            gW.mLineX[gW.mLine][gW.mLineLen[gW.mLine]] = static_cast<uint8_t>(gW.mCursor > 255 ? 255 : gW.mCursor);
            gW.mLineLen[gW.mLine]++;
        }
        return;
    }
    if (c == '\n')
    {
        NewLine();
        return;
    }
    if (!HasGlyph(c))
    {
        gW.mProgress++; // "Missing character" (e.g. the '^' pause marker when it was stepped over)
        return;
    }
    if (!KeepInBounds())
    {
        return;
    }
    while (gW.mLineCount - 1 < gW.mLine && gW.mLineCount < kMaxLines)
    {
        gW.mLineLen[gW.mLineCount++] = 0;
    }
    if (gW.mLine >= kMaxLines)
    {
        return;
    }
    const int n = gW.mLineLen[gW.mLine];
    if (n < kMaxLineChars)
    {
        gW.mLines[gW.mLine][n] = c;
        gW.mLineX[gW.mLine][n] = static_cast<uint8_t>(gW.mCursor > 255 ? 255 : gW.mCursor);
        gW.mLineLen[gW.mLine]++;
        DrawGlyph(c, kTextX + gW.mCursor, kTextY + gW.mFirstLineY + gW.mLine * kLineSep, kWhite, true);
        gUploadNeeded = true;
    }
    gW.mProgress++;
    if ((c == '.' || c == '!' || c == '?') && gW.mProgress < gW.mLen && gW.mText[gW.mProgress] == ' ')
    {
        NewLine();
    }
}

void RemoveFirstLine()
{
    if (gW.mLineCount == 0)
    {
        return;
    }
    for (int l = 1; l < gW.mLineCount; l++)
    {
        memcpy(gW.mLines[l - 1], gW.mLines[l], kMaxLineChars);
        memcpy(gW.mLineX[l - 1], gW.mLineX[l], kMaxLineChars);
        gW.mLineLen[l - 1] = gW.mLineLen[l];
    }
    gW.mLineCount--;
    gW.mLineLen[gW.mLineCount] = 0;
    gW.mLine--;
    gW.mFirstLineY += kLineSep;
    RedrawText();
}

void PushTextUp()
{
    gW.mFirstLineY -= kLineSep / 2;
    RedrawText();
}

// Writer.Update: steps the typewriter at mSpeed chars/second.
void WriterUpdate()
{
    gW.mJustWrote = false;
    if (AtEndOfBox() || AtEndOfText())
    {
        return;
    }
    gW.mStep += gW.mSpeed * 65536 / 60;
    while (gW.mStep >= 65536)
    {
        gW.mStep -= 65536;
        if (!AtEndOfBox() && !AtEndOfText())
        {
            WriteStep();
        }
        gW.mJustWrote = true;
    }
}

// ---- DialogueState.cs ----------------------------------------------------------------------------------------------
enum class Base
{
    Waiting,
    Bump,
    Done,
};
enum class Top
{
    None,
    Writing,
    WaitCaret,
};

Base gBase = Base::Waiting;
Top gTopState = Top::None;
int gNormalSpeed = 30;
int gSpeedScale = 2;
int gBumpTimer = 0;
int gHalfBumps = 0;
int gLinesBumped = 0;
int gBlinkyTimer = 0;
bool gBlinkyOn = false;
bool gBlinkyDrawn = false;
bool gBlinkyEnabled = true;

void PlaySfx(int32_t id)
{
    if (id >= 0)
    {
        AudioManager_PlaySFX(static_cast<uint32_t>(id));
    }
}

bool AcceptOrCancelHeld()
{
    return key_is_down(KEY_A) || key_is_down(KEY_B);
}

bool AcceptOrCancelHit()
{
    return key_hit(KEY_A) || key_hit(KEY_B);
}

void DrawBlinky(bool on)
{
    if (gIntro)
    {
        // UILabel prompt "A" in red, bottom-right.
        const int x = kBoxW - 4 - kCharW;
        for (int r = 0; r < 8; r++)
        {
            for (int c = 0; c < 7; c++)
            {
                SetPixel(gCanvas, x + c, kBoxH - 14 + r, kFill);
            }
        }
        if (on)
        {
            DrawGlyph('A', x, kBoxH - 14, kPromptRed, false);
        }
    }
    else
    {
        // dialogue_blinky_box.png: a small red arrow
        static const char kArrow[8][9] = {
            ".RRRRR..", ".RRRRR..", ".rRRRr..", ".#rRr#..", "..#r#...", "...#....", "........", "........",
        };
        const int bx = kBoxW - 10;
        const int by = kBoxH - 10;
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 8; x++)
            {
                const char p = kArrow[y][x];
                if (p == '.')
                {
                    SetPixel(gCanvas, bx + x, by + y, kFill);
                }
                else if (on)
                {
                    SetPixel(gCanvas, bx + x, by + y, p == 'R' ? kRedLight : p == 'r' ? kRedDark : kBlack);
                }
            }
        }
    }
    gUploadNeeded = true;
}

void Upload()
{
    // Tiles of each OBJ, in the same order as the canvas array.
    int tile = 0;
    for (int o = 0; o < kObjCount; o++)
    {
        const int n = o < 3 ? 32 : 8;
        memcpy32(gVram->ptr(gObjBlock[o]), &gCanvas[tile * 8], n * 8);
        tile += n;
    }
    gUploadNeeded = false;
}

void PlaceObjs()
{
    const int y = gTop ? kBoxYTop : kBoxYDown;
    for (int o = 0; o < kObjCount; o++)
    {
        const int px = o < 3 ? kBoxX + o * 64 : kBoxX + (o - 3) * 32;
        const int py = o < 3 ? y : y + 32;
        const uint16_t a1 = o < 3 ? ATTR1_SIZE_64 : ATTR1_SIZE_32;
        obj_set_attr(&oam_mem[kFirstOam + o], ATTR0_Y(py) | ATTR0_BLEND | ATTR0_4BPP | ATTR0_WIDE,
                     ATTR1_X(px) | a1, ATTR2_ID(gObjBlock[o]) | ATTR2_PRIO(0) | ATTR2_PALBANK(kPalBank));
    }
}

void HideObjs()
{
    for (int o = 0; o < kObjCount; o++)
    {
        obj_hide(&oam_mem[kFirstOam + o]);
    }
}

void EnterWriting()
{
    gTopState = Top::Writing;
}

void FinishState()
{
    gBase = Base::Done;
    gTopState = Top::None;
}

void DoBump()
{
    if (gHalfBumps == 0)
    {
        RemoveFirstLine();
    }
    if (gHalfBumps < 2)
    {
        gHalfBumps++;
        PushTextUp();
    }
    else
    {
        gHalfBumps = 0;
        gLinesBumped++;
        if (gLinesBumped == kLinesPerBox)
        {
            gBase = Base::Waiting;
        }
        EnterWriting();
    }
}
} // namespace

// ---- Public API ---------------------------------------------------------------------------------------------------
void Dialog_Init(ObjVramAllocator& vram)
{
    gVram = &vram;
    gOpen = false;
    gDialogueOpen = 0;
    memset(gSceneLine, 0, sizeof(gSceneLine));
    memset(gSceneFinished, 0, sizeof(gSceneFinished));
    gSfxBlip = AudioManager_FindSfx("dialogue_blip");
    gSfxBloop = AudioManager_FindSfx("dialogue_bloop");
    BuildBoxImage();
}

void Dialog_SetArea(const char* mapName)
{
    gArea = mapName;
}

const char* Dialog_GetFor(const char* npc, const char* area, const char* scene, int id, bool* top)
{
    unsigned index = 0;
    const DialogSceneData* s = FindScene(npc, area, scene, &index);
    if (!s || s->mLineCount == 0)
    {
        return "No text available.";
    }
    if (top)
    {
        *top = s->mTop != 0;
    }
    if (id == -1)
    {
        id = gSceneLine[index];
    }
    if (id >= s->mLineCount)
    {
        id = s->mLoop;
    }
    const char* line = kDialogLines[s->mFirstLine + id];
    id++;
    if (id >= s->mLineCount)
    {
        gSceneFinished[index] = 1;
    }
    gSceneLine[index] = static_cast<uint8_t>(id);
    return line;
}

const char* Dialog_Get(const char* npc, const char* scene, int id, bool* top)
{
    return Dialog_GetFor(npc, gArea, scene, id, top);
}

void Dialog_Open(const char* text, bool top, bool intro)
{
    if (!gVram || gOpen)
    {
        return;
    }
    Dialog_HideLabels();
    for (int o = 0; o < kObjCount; o++)
    {
        gObjBlock[o] = gVram->alloc((o < 3 ? 32 : 8) * ObjVramAllocator::BlockSize);
        if (gObjBlock[o] < 0)
        {
            for (int p = 0; p < o; p++)
            {
                gVram->free(gObjBlock[p]);
            }
            return;
        }
    }
    gTop = top;
    gIntro = intro;
    gOpen = true;
    gDialogueOpen = 1;

    pal_obj_mem[kPalBank * 16 + 0] = 0;
    pal_obj_mem[kPalBank * 16 + kFill] = RGB15(5, 5, 6);
    pal_obj_mem[kPalBank * 16 + kBlack] = RGB15(0, 0, 0);
    pal_obj_mem[kPalBank * 16 + kWhite] = RGB15(31, 31, 31);
    pal_obj_mem[kPalBank * 16 + kRedLight] = RGB15(31, 11, 9);
    pal_obj_mem[kPalBank * 16 + kRedDark] = RGB15(24, 0, 7);
    pal_obj_mem[kPalBank * 16 + kPromptRed] = RGB15(31, 4, 4);

    // Semi-transparent OBJs blend with everything beneath (the box texture is 80% opaque on the PC).
    gSavedBldCnt = REG_BLDCNT;
    gSavedBldAlpha = REG_BLDALPHA;
    REG_BLDCNT = (BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP) << 8;
    REG_BLDALPHA = BLDA_BUILD(13, 3);

    gNormalSpeed = 30;
    gSpeedScale = intro ? 4 : 2;
    ResetWriter(text);
    gW.mSpeed = gNormalSpeed;
    memcpy(gCanvas, gBoxImage, sizeof(gCanvas));
    Upload();
    PlaceObjs();

    gBase = Base::Waiting;
    EnterWriting();
    gBumpTimer = 0;
    gHalfBumps = 0;
    gLinesBumped = 0;
    gBlinkyTimer = 24;
    gBlinkyOn = false;
    gBlinkyDrawn = false;
    gBlinkyEnabled = true;
}

void Dialog_ShowLabels(const char* const* lines, const int* xs, const int* ys, int count, int top, int selectorX,
                       int selectorY)
{
    if (!gVram || gOpen)
    {
        return;
    }
    if (!gLabels)
    {
        for (int o = 0; o < kObjCount; o++)
        {
            gObjBlock[o] = gVram->alloc((o < 3 ? 32 : 8) * ObjVramAllocator::BlockSize);
            if (gObjBlock[o] < 0)
            {
                for (int p = 0; p < o; p++)
                {
                    gVram->free(gObjBlock[p]);
                }
                return;
            }
        }
        gLabels = true;
    }
    pal_obj_mem[kPalBank * 16 + 0] = 0;
    pal_obj_mem[kPalBank * 16 + kBlack] = RGB15(0, 0, 0);
    memset(gCanvas, 0, sizeof(gCanvas));
    for (int i = 0; i < count; i++)
    {
        int x = xs[i];
        for (const char* c = lines[i]; *c; c++)
        {
            if (HasGlyph(*c) && *c != ' ')
            {
                DrawGlyph(*c, x, ys[i] - top, kBlack, false);
            }
            x += kCharW;
        }
    }
    if (selectorY >= 0)
    {
        // MenuSelector: a small arrow pointing right.
        for (int r = 0; r < 5; r++)
        {
            const int len = r < 3 ? r + 1 : 5 - r;
            for (int c = 0; c < len; c++)
            {
                SetPixel(gCanvas, selectorX + c, selectorY - top + r, kBlack);
            }
        }
    }
    Upload();
    for (int o = 0; o < kObjCount; o++)
    {
        const int px = o < 3 ? 40 + o * 64 : 40 + (o - 3) * 32;
        const int py = o < 3 ? top : top + 32;
        const uint16_t a1 = o < 3 ? ATTR1_SIZE_64 : ATTR1_SIZE_32;
        obj_set_attr(&oam_mem[kFirstOam + o], ATTR0_Y(py) | ATTR0_4BPP | ATTR0_WIDE, ATTR1_X(px) | a1,
                     ATTR2_ID(gObjBlock[o]) | ATTR2_PRIO(0) | ATTR2_PALBANK(kPalBank));
    }
}

void Dialog_HideLabels()
{
    if (!gLabels)
    {
        return;
    }
    HideObjs();
    for (int o = 0; o < kObjCount; o++)
    {
        gVram->free(gObjBlock[o]);
    }
    gLabels = false;
}

bool Dialog_IsOpen()
{
    return gOpen;
}

void Dialog_Close()
{
    if (!gOpen)
    {
        return;
    }
    HideObjs();
    for (int o = 0; o < kObjCount; o++)
    {
        gVram->free(gObjBlock[o]);
    }
    REG_BLDCNT = gSavedBldCnt;
    REG_BLDALPHA = gSavedBldAlpha;
    gOpen = false;
    gDialogueOpen = 0;
}

bool Dialog_Update()
{
    if (!gOpen)
    {
        return false;
    }

    // DialogueState.Update: hold Accept/Cancel to type faster.
    gW.mSpeed = AcceptOrCancelHeld() ? gNormalSpeed * gSpeedScale : gNormalSpeed;

    // TextBox.Update: the writer only runs in the Writing state; the arrow blinks while waiting.
    const bool paused = gTopState != Top::Writing;
    if (!paused)
    {
        WriterUpdate();
    }
    if (gBlinkyEnabled && (paused || AtEndOfBox() || AtEndOfText()))
    {
        if (--gBlinkyTimer < 0)
        {
            gBlinkyTimer = 24;
            gBlinkyOn = !gBlinkyOn;
        }
    }
    else
    {
        gBlinkyOn = false;
    }
    if (gBlinkyOn != gBlinkyDrawn)
    {
        gBlinkyDrawn = gBlinkyOn;
        DrawBlinky(gBlinkyOn);
    }

    // The state machine (top of the stack first).
    if (gTopState == Top::Writing)
    {
        if (gW.mJustWrote)
        {
            PlaySfx(gSfxBlip);
        }
        if (AtEndOfText() || AtEndOfBox() || NextChar() == '^')
        {
            if (NextChar() == '^')
            {
                gW.mProgress++;
                gTopState = Top::WaitCaret;
            }
            else
            {
                gTopState = Top::None;
                if (AtEndOfText())
                {
                    gBase = Base::Waiting;
                }
            }
        }
    }
    else if (gTopState == Top::WaitCaret)
    {
        if (AcceptOrCancelHeld())
        {
            PlaySfx(gSfxBloop);
            gTopState = Top::Writing;
        }
    }
    else if (gBase == Base::Bump)
    {
        if (++gBumpTimer >= 6)
        {
            gBumpTimer = 0;
            DoBump();
        }
    }
    else if (gBase == Base::Waiting)
    {
        if (AtEndOfText() && AcceptOrCancelHit())
        {
            PlaySfx(gSfxBloop);
            FinishState();
        }
        else if (!AtEndOfText() && AcceptOrCancelHeld())
        {
            PlaySfx(gSfxBloop);
            if (AtEndOfBox())
            {
                gBase = Base::Bump;
                gBlinkyEnabled = false;
                gBlinkyOn = false;
                gLinesBumped = 0;
                gHalfBumps = 0;
                gBumpTimer = 0;
                DoBump(); // the first doBump fires as the state is entered
            }
            else
            {
                EnterWriting();
            }
        }
    }
    if (gBase != Base::Bump)
    {
        gBlinkyEnabled = true;
    }

    if (gUploadNeeded)
    {
        Upload();
    }
    if (gBase == Base::Done)
    {
        Dialog_Close();
        return false;
    }
    return true;
}
