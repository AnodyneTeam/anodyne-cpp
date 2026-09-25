
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "dialog_manager.h"
#include "tonc.h"

#include "obj_vram_allocator.hpp"
#include "audio_manager.h"

struct MapPalData final
{
    uint32_t mPalSize = 0;
    const uint16_t* mPalData = nullptr;
};

struct MapTileSetData final
{
    uint32_t mTileSetSize = 0;
    const uint8_t* mTileSetData = nullptr;
};

struct MapScreenBlockData final
{
    uint32_t mMapWidth = 0;
    uint32_t mMapHeight = 0;
    const uint16_t* mMapData = nullptr;
};

struct MapEntry final
{
    MapPalData mPalData;
    MapTileSetData mTileSetData;
    MapScreenBlockData mScreenBlockData;
};

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

void CopyMapRegionToBG0(int mapX, int mapY)
{
    const int MAP_W = kMaps[gCurrentMap].mScreenBlockData.mMapWidth;
    const int MAP_H = kMaps[gCurrentMap].mScreenBlockData.mMapHeight;

    const int VIEW_W = 32;
    const int VIEW_H = 32;

    for (int y = 0; y < VIEW_H; y++)
    {
        int srcY = mapY + y;
        if (srcY < 0) srcY = 0;
        if (srcY >= MAP_H) srcY = MAP_H - 1;

        for (int x = 0; x < VIEW_W; x++)
        {
            int srcX = mapX + x;
            if (srcX < 0) srcX = 0;
            if (srcX >= MAP_W) srcX = MAP_W - 1;

            // ROW-MAJOR INDEXING — THIS IS THE IMPORTANT PART
            uint16_t tile = kMaps[gCurrentMap].mScreenBlockData.mMapData[srcY * MAP_W + srcX];

            se_mem[31][y * VIEW_W + x] = tile;
        }
    }
}

static void SetCurrentMap(uint32_t mapIndex)
{
    gCurrentMap = mapIndex;

    // --- Palette (256 halfwords) ---
    memcpy16(pal_bg_mem, kMaps[gCurrentMap].mPalData.mPalData, kMaps[gCurrentMap].mPalData.mPalSize);

    // --- Tiles (8bpp, byte data -> halfword count) ---
    memcpy16(tile_mem, kMaps[gCurrentMap].mTileSetData.mTileSetData, kMaps[gCurrentMap].mScreenBlockData.mMapWidth * kMaps[gCurrentMap].mScreenBlockData.mMapHeight);

    CopyMapRegionToBG0(0, 0);
    // --- Map (uint16_t entries) ---
   // memcpy16(se_mem[31], gMapData, sizeof(gMapData) / 2);

   /// for (int i = 0; i < 1024; i++)
    //    se_mem[31][i] = 5;

}

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
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0 | DCNT_OBJ;
    REG_BG1CNT = 0;
    REG_BG2CNT = 0;
    REG_BG3CNT = 0;

    REG_BG0CNT = BG_CBB(0) | BG_SBB(31) | BG_8BPP | BG_REG_32x32;
    SetCurrentMap(gCurrentMap);
}

int main()
{
    // Standard cart ROM timing (3/1 waitstates + prefetch) instead of the
    // power-on 4/2 with no prefetch - everything still running from ROM
    // (Thumb code, const data, the audio blob) gets substantially faster.
    REG_WAITCNT = WS_STANDARD;

    AudioManager_Init();
    AudioManager_PlayBGM(0); // auto-start for easier testing

    AppMode mode = AppMode::AudioTest;
    EnterAudioTestMode();
    bool modeComboWasDown = false;

    // --- Audio test state ---
    uint32_t debugBgmIndex = 0;
    uint32_t debugSfxIndex = 0;
    int hudStatsTimer = 0;
    uint32_t hudIsrLast = 0;
    uint32_t hudIsrMax = 0;

    // --- Map viewer state ---
    int camX = 8 * 10;
    int camY = 8 * 10;
    int lastTileX = -1;
    int lastTileY = -1;
    int scrollTimer = 0;

    ObjVramAllocator objVram;
    int id = objVram.alloc(512);
    TILE* pYoung = objVram.ptr(id);
    (void)pYoung;

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
                lastTileX = -1;
                lastTileY = -1;
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
                SetCurrentMap(gCurrentMap);
                camX = 8 * 10;
                camY = 8 * 10;
                lastTileX = -1;
                lastTileY = -1;
            }

            if (key_hit(KEY_SELECT) && !modeComboDown)
            {
                gCurrentMap = (gCurrentMap == 0) ? (kMapCount - 1) : (gCurrentMap - 1);
                SetCurrentMap(gCurrentMap);
                camX = 8 * 10;
                camY = 8 * 10;
                lastTileX = -1;
                lastTileY = -1;
            }

            // ------------------------------------------------------------
            // SCROLLING (slow)
            // ------------------------------------------------------------
            scrollTimer++;
            if (scrollTimer >= 120)
            {
                scrollTimer = 0;
                if (key_is_down(KEY_LEFT))  camX--;
                if (key_is_down(KEY_RIGHT)) camX++;
                if (key_is_down(KEY_UP))    camY--;
                if (key_is_down(KEY_DOWN))  camY++;
            }

            // ------------------------------------------------------------
            // CAMERA CLAMPING
            // ------------------------------------------------------------
            int mapWpx = kMaps[gCurrentMap].mScreenBlockData.mMapWidth  * 8;
            int mapHpx = kMaps[gCurrentMap].mScreenBlockData.mMapHeight * 8;

            if (camX < 0) camX = 0;
            if (camY < 0) camY = 0;
            if (camX > mapWpx - 240) camX = mapWpx - 240;
            if (camY > mapHpx - 160) camY = mapHpx - 160;

            // ------------------------------------------------------------
            // TILE + SUBPIXEL SCROLLING
            // ------------------------------------------------------------
            int tileX = camX >> 3;
            int tileY = camY >> 3;
            int subX = camX & 7;
            int subY = camY & 7;

            REG_BG0HOFS = subX;
            REG_BG0VOFS = subY;

            // ------------------------------------------------------------
            // TILE WINDOW UPDATE
            // ------------------------------------------------------------
            if (tileX != lastTileX || tileY != lastTileY)
            {
                CopyMapRegionToBG0(tileX, tileY);
                lastTileX = tileX;
                lastTileY = tileY;
            }
        }

        AudioManager_Update();
    }
}
