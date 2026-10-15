
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "dialog_manager.h"
#include "tonc.h"

#include "obj_vram_allocator.hpp"
#include "audio_manager.h"
#include "sprite.h"
#include "map_data.h"
#include "world.h"

#include "../build/tools/map_registry.h"

// kMapCount is provided from map_registry_.h
static uint32_t gCurrentMap = 0;

/*
static void dialog_manager_test(void)
{
    struct dialog_manager dm;
    struct binary_blob data;
    dialog_manager_init(&dm, &data);

    dialog_manager_random(&dm, npc_id_empty1, scene_id_empty3);

    dialog_manager_free(&dm);
}
*/

// HUD text is drawn over itself in place (see the audio test loop), so
// every line is padded to the full screen width to overwrite old text.
static const int kHudCols = 30; // 240px / 8px font
static char lastHudBuf[5 * (kHudCols + 1) + 1];

// Formats one HUD line into `out`, truncated/space-padded to exactly
// kHudCols columns plus '\n'. Returns the position after it.
static char* HudLine(char* out, const char* fmt, ...)
{
    char line[64];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (len < 0)
    {
        len = 0;
    }
    for (int i = 0; i < kHudCols; i++)
    {
        out[i] = (i < len && i < (int)sizeof(line) - 1) ? line[i] : ' ';
    }
    out[kHudCols] = '\n';
    out[kHudCols + 1] = '\0';
    return out + kHudCols + 1;
}

// The audio test HUD (BG1) and the map viewer (BG0) fought over VRAM/display
// priority when both were active together (BG0's opaque map tiles hid BG1's
// text). Until that's sorted out properly, keep them mutually exclusive at
// runtime - hold START+SELECT together to switch between them. Audio itself
// (the VBlank-driven refill) doesn't care which mode is showing and keeps
// playing across a switch.
enum class AppMode
{
    AudioTest,
    MapViewer
};

static void EnterAudioTestMode()
{
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG1;
    REG_BG0CNT = 0;
    REG_BG2CNT = 0;
    REG_BG3CNT = 0;

    tte_init_se_default(1, BG_CBB(1) | BG_SBB(28));
    lastHudBuf[0] = '\0'; // force a full HUD redraw on (re)entry
    // Deliberately not using tte_printf (= iprintf): that needs tte_init_con()
    // to hook stdio into tte's console, and this vendored libtonc declares
    // tte_init_con()/tte_con_write() in tonc_tte.h but ships no .c file that
    // implements either - so tte_printf would silently draw nothing (its
    // characters went to the default no-op _write stub, see syscalls.c).
    // sprintf (no stdio redirection involved) + tte_write() (tte's own direct
    // string-draw call) works with what's actually implemented here.
}

static void EnterMapViewerMode()
{
    REG_BG1CNT = 0;
    REG_BG2CNT = 0;
    REG_BG3CNT = 0;
    World_Enter();
    World_Load(kMaps[gCurrentMap], kMapNames[gCurrentMap], kMapAudio[gCurrentMap], kMapScroll[gCurrentMap]);
}

// ---------------------------------------------------------------------------------------------------------
// Sprite gallery (test): every compiled sheet, packed left to right in rows (any size, big ones use several
// OBJs), each playing its first anim (or stepping through its frames if it has none). A page ends when the
// screen, the free palette banks, OAM or OBJ VRAM run out. The world owns OAM slots 0-9 / palette banks 0-3.
// ---------------------------------------------------------------------------------------------------------
static const int kGalleryFirstOam = 10;     // OAM slots 0-9 and palette banks 0-3 belong to the world (see world.cpp)
static const int kGalleryFirstBank = 4;
static const int kGalleryMaxSprites = 12;   // palette banks 4..15
static const int kGalleryMargin = 4;
static const int kGalleryMaxPages = 64;

static Sprite gGallery[kGalleryMaxSprites];
static int gGalleryX[kGalleryMaxSprites];
static int gGalleryY[kGalleryMaxSprites];
static int gGalleryFrameTimer[kGalleryMaxSprites];
// First sheet index of each page, learned as pages are loaded (page N+1's start is known once N is loaded).
static uint32_t gGalleryPageStart[kGalleryMaxPages + 1];
static int gGalleryPageCount = 0; // pages discovered so far
static bool gGalleryHasMore = true;

static void GalleryClear(ObjVramAllocator& vram)
{
    for (Sprite& s : gGallery)
    {
        Sprite_Destroy(s, vram);
    }
}

static void GalleryLoadPage(ObjVramAllocator& vram, int page)
{
    GalleryClear(vram);

    int slot = 0;
    int oamNext = kGalleryFirstOam;
    int curX = 0;
    int curY = 0;
    int rowH = 0;
    uint32_t i = gGalleryPageStart[page];
    for (; i < Sprite_SheetCount() && slot < kGalleryMaxSprites; i++)
    {
        const SpriteSheetEntry& sheet = *Sprite_SheetAt(i);
        if (!Sprite_IsDrawable(sheet))
        {
            continue;
        }

        const int w = sheet.mTilesW * 8;
        const int h = sheet.mTilesH * 8;
        int x = curX;
        int y = curY;
        int newRowH = rowH;
        if (x + w > 240 && x > 0)
        {
            x = 0;
            y = curY + rowH + kGalleryMargin;
            newRowH = 0;
        }
        if (y + h > 160 && slot > 0)
        {
            break; // page full - this sheet starts the next one
        }
        if (oamNext + static_cast<int>(sheet.mPartCount) > 128)
        {
            break;
        }

        if (!Sprite_Create(gGallery[slot], vram, sheet.mName, slot + kGalleryFirstBank, oamNext, sheet.mFrameWidth,
                           sheet.mFrameHeight))
        {
            if (slot > 0)
            {
                break; // out of OBJ VRAM - next page
            }
            continue; // can't ever fit on its own: skip it
        }

        if (sheet.mAnimCount > 0)
        {
            Sprite_Play(gGallery[slot], sheet.mAnims[0].mName);
        }
        gGalleryFrameTimer[slot] = 0;
        gGalleryX[slot] = x;
        gGalleryY[slot] = y;
        oamNext += static_cast<int>(sheet.mPartCount);
        curX = x + w + kGalleryMargin;
        curY = y;
        rowH = (newRowH > h ? newRowH : h);
        slot++;
    }

    // Skip trailing undrawable sheets so "more pages" is accurate.
    while (i < Sprite_SheetCount() && !Sprite_IsDrawable(*Sprite_SheetAt(i)))
    {
        i++;
    }
    gGalleryHasMore = i < Sprite_SheetCount();
    if (gGalleryHasMore && page + 1 <= kGalleryMaxPages)
    {
        gGalleryPageStart[page + 1] = i;
        if (page + 2 > gGalleryPageCount)
        {
            gGalleryPageCount = page + 2;
        }
    }
    else if (page + 1 > gGalleryPageCount)
    {
        gGalleryPageCount = page + 1;
    }
}

static void GalleryUpdateAndDraw()
{
    for (int i = 0; i < kGalleryMaxSprites; i++)
    {
        Sprite& s = gGallery[i];
        if (!s.mSheet)
        {
            continue;
        }

        if (s.mAnim < 0 && s.mSheet->mFrameCount > 1)
        {
            // No anims in the C# for this sheet: step through its frames at 4 fps.
            if (++gGalleryFrameTimer[i] >= 15)
            {
                gGalleryFrameTimer[i] = 0;
                Sprite_SetFrame(s, (s.mStaticFrame + 1) % s.mSheet->mFrameCount);
            }
        }

        Sprite_Update(s);
        Sprite_Draw(s, gGalleryX[i], gGalleryY[i]);
    }
}

int main()
{
    // Standard cart ROM timing (3/1 waitstates + prefetch) instead of the
    // power-on 4/2 with no prefetch - everything still running from ROM
    // (Thumb code, const data, the audio blob) gets substantially faster.
    REG_WAITCNT = WS_STANDARD;

    AudioManager_Init();
    // (The world starts the map's music itself, see World_Load.)

    // Start in the map viewer (audio is muted for now); START+SELECT still toggles to the audio test HUD.
    AppMode mode = AppMode::MapViewer;
    EnterMapViewerMode();
    bool modeComboWasDown = false;

    // --- Audio test state ---
    uint32_t debugBgmIndex = 0;
    uint32_t debugSfxIndex = 0;
    int hudStatsTimer = 0;
    uint32_t hudIsrLast = 0;
    uint32_t hudIsrMax = 0;

    oam_init(oam_mem, 128);

    // The world (map + player, ported from the C#) owns OAM slot 0 / OBJ palette bank 0; the sprite gallery test
    // uses the rest. A toggles between them in the map viewer.
    ObjVramAllocator objVram;
    World_Init(objVram);
    World_Load(kMaps[gCurrentMap], kMapNames[gCurrentMap], kMapAudio[gCurrentMap], kMapScroll[gCurrentMap]); // again: the map's overlay sprites need the VRAM allocator
    bool galleryOn = false;
    int galleryPage = 0;
    GalleryLoadPage(objVram, galleryPage);

    while (1)
    {
        // The loop was running unsynced to the display (no vsync wait), so
        // the HUD's every-frame erase+redraw could land mid-scan and tear/
        // flicker - VBlankIntrWait() (relies on the VBlank ISR clearing the
        // BIOS's REG_IFBIOS flag, which tonc's irq_init/master dispatcher
        // already does) throttles this loop to once per real 60Hz frame.
        VBlankIntrWait();
        key_poll();

        // ------------------------------------------------------------
        // MODE SWITCH (hold START+SELECT together)
        // ------------------------------------------------------------
        bool modeComboDown = key_is_down(KEY_START) && key_is_down(KEY_SELECT);
        if (modeComboDown && !modeComboWasDown)
        {
            mode = (mode == AppMode::AudioTest) ? AppMode::MapViewer : AppMode::AudioTest;
            if (mode == AppMode::AudioTest)
            {
                EnterAudioTestMode();
            }
            else
            {
                EnterMapViewerMode();
                galleryOn = false;
            }
        }
        modeComboWasDown = modeComboDown;

        if (mode == AppMode::AudioTest)
        {
            // ------------------------------------------------------------
            // AUDIO TEST CONTROLS
            // L/R cycle the selected SFX, A plays it, B starts/advances BGM -
            // both play independently (FIFO A/B), so this lets you confirm
            // SFX audibly layers on top of BGM rather than interrupting it.
            // ------------------------------------------------------------
            if (key_hit(KEY_L) && AudioManager_GetSfxCount() > 0)
            {
                debugSfxIndex = (debugSfxIndex + AudioManager_GetSfxCount() - 1) % AudioManager_GetSfxCount();
            }
            if (key_hit(KEY_R) && AudioManager_GetSfxCount() > 0)
            {
                debugSfxIndex = (debugSfxIndex + 1) % AudioManager_GetSfxCount();
            }
            if (key_hit(KEY_A) && AudioManager_GetSfxCount() > 0)
            {
                AudioManager_PlaySFX(debugSfxIndex);
            }
            if (key_hit(KEY_B) && AudioManager_GetBgmCount() > 0)
            {
                debugBgmIndex = (debugBgmIndex + 1) % AudioManager_GetBgmCount();
                AudioManager_PlayBGM(debugBgmIndex);
            }

            // ISR stats change every frame; sample them twice a second so the
            // HUD text (and so the redraw below) only changes that often.
            if (++hudStatsTimer >= 30)
            {
                hudStatsTimer = 0;
                hudIsrLast = AudioManager_GetIsrCyclesLast();
                hudIsrMax = AudioManager_GetIsrCyclesMax();
            }

            // Every line padded to the full 30-column width and written over
            // the old text in place - no tte_erase_screen(). Erasing then
            // redrawing every frame flickered whenever the audio ISR ran
            // past the end of VBlank (e.g. several SFX decoding at once): the
            // erase landed mid-scanout and the blank screen was visible.
            char hudBuf[5 * (kHudCols + 1) + 1];
            char* p = hudBuf;
            p = HudLine(p, "BGM %d/%d: %s", (int)debugBgmIndex, (int)AudioManager_GetBgmCount(),
                        AudioManager_GetBgmName(debugBgmIndex));
            p = HudLine(p, "SFX %d/%d: %s", (int)debugSfxIndex, (int)AudioManager_GetSfxCount(),
                        AudioManager_GetSfxName(debugSfxIndex));
            p = HudLine(p, "A:SFX L/R:pick B:next BGM");
            p = HudLine(p, "START+SELECT: map viewer");
            p = HudLine(p, "ISR %lu cyc (max %lu)", (unsigned long)hudIsrLast, (unsigned long)hudIsrMax);

            if (strcmp(hudBuf, lastHudBuf) != 0)
            {
                strcpy(lastHudBuf, hudBuf);
                // tte_write() advances the cursor and never resets it on its
                // own - without this it walks off screenblock 28 into other
                // VRAM (this once corrupted BG0's map and more).
                tte_set_pos(0, 0);
                tte_write(hudBuf);
            }
        }
        else
        {
            // ------------------------------------------------------------
            // MAP CYCLING (START/SELECT alone - the combo above is caught
            // before key_hit() would see either as a single press)
            // ------------------------------------------------------------
            if (key_hit(KEY_START) && !modeComboDown)
            {
                gCurrentMap = (gCurrentMap + 1) % kMapCount;
                World_Load(kMaps[gCurrentMap], kMapNames[gCurrentMap], kMapAudio[gCurrentMap], kMapScroll[gCurrentMap]);
            }

            if (key_hit(KEY_SELECT) && !modeComboDown)
            {
                gCurrentMap = (gCurrentMap == 0) ? (kMapCount - 1) : (gCurrentMap - 1);
                World_Load(kMaps[gCurrentMap], kMapNames[gCurrentMap], kMapAudio[gCurrentMap], kMapScroll[gCurrentMap]);
            }

            // A toggles the sprite gallery (full-screen, so the 160x160 playfield window is off meanwhile);
            // R/L change its page.
            if (key_hit(KEY_A))
            {
                galleryOn = !galleryOn;
                if (galleryOn)
                {
                    World_SetPlayerVisible(false);
                    REG_DISPCNT = REG_DISPCNT & ~DCNT_WIN0;
                }
                else
                {
                    for (const Sprite& g : gGallery)
                    {
                        if (g.mSheet)
                        {
                            Sprite_Hide(g);
                        }
                    }
                    REG_DISPCNT = REG_DISPCNT | DCNT_WIN0;
                }
            }
            if (galleryOn && (key_hit(KEY_R) || key_hit(KEY_L)))
            {
                if (key_hit(KEY_R))
                {
                    galleryPage = gGalleryHasMore ? galleryPage + 1 : 0; // wrap after the last page
                }
                else if (galleryPage > 0)
                {
                    galleryPage--;
                }
                else
                {
                    // Backwards from page 0: walk forward to find the last page.
                    while (gGalleryHasMore)
                    {
                        GalleryLoadPage(objVram, galleryPage);
                        if (gGalleryHasMore)
                        {
                            galleryPage++;
                        }
                    }
                }
                GalleryLoadPage(objVram, galleryPage);
            }

            if (galleryOn)
            {
                GalleryUpdateAndDraw();
            }
            else
            {
                World_Update();
            }
        }

        AudioManager_Update();
    }
}
