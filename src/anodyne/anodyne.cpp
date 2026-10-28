
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "credits.h"
#include "save.h"
#include "dialog.h"
#include "hud.h"
#include "tonc.h"

#include "obj_vram_allocator.hpp"
#include "audio_manager.h"
#include "screen_fx.hpp"
#include "soft_mosaic.hpp"
#include "sprite.h"
#include "map_data.h"
#include "tex_dark.h"
#include "world.h"
#include "entities.h"
#include "pause.h"
#include "title.h"
#include "pixelate_test.hpp"

#include "map_registry.h"
#include "game_state.h"
#include "game_probe.h"
#ifdef ANODYNE_LINK
#include "link_ipc.h"
#endif

static_assert(kEntityPresetCount <= kMaxPresets, "more entity presets than game_state.h has room for");
static_assert(kMapCount <= static_cast<uint32_t>(kMaxMaps), "more maps than game_state.h has room for");

// kMapCount is provided from map_registry_.h
static uint32_t gCurrentMap = 0;


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
    MapViewer,
    Title,
    Intro, // IntroState: black screen, the sage's opening text, then the new game starts in BLANK
    PixelateTest,
    Credits // CreditsState: after the ending, back to the title
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

// Hides half-built screens while maps load: a full darken of every layer and the backdrop (forced blank would show
// white). The world's windows let it through (WIN_BLD), so it also covers the margins around the playfield.
static void ScreenBlack(bool on)
{
    ScreenDarken(on ? 16 : 0);
}

int Game_MapIndex(const char* name)
{
    for (uint32_t i = 0; i < kMapCount; i++)
    {
        if (strcmp(kMapNames[i], name) == 0)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// PlayState.Warp: GlobalState.ReturnTarget = the Nexus door of this map's Nexus pad (EntityManager.
// GetNexusGateForCurrentMap), else, once the Nexus has been visited, its dungeon entrance; else none.
static void SetReturnTargetToNexus()
{
    const MapDoorList& doors = kMapDoors[gCurrentMap];
    const MapDoor* pad = nullptr;
    int pads = 0;
    for (uint32_t i = 0; i < doors.mCount; i++)
    {
        if (doors.mDoors[i].mType == 16) // NexusPad
        {
            pad = &doors.mDoors[i];
            pads++;
        }
    }
    if (pads == 1)
    {
        // (the pad's trigger warps to its linked door + (10, 34), the offset the menu's warp adds back)
        gGame.mReturnMap = pad->mDestMap;
        gGame.mReturnX = pad->mDestX - 10;
        gGame.mReturnY = pad->mDestY - 34;
        return;
    }
    gGame.mReturnMap = -1;
    const int nexus = Game_MapIndex("NEXUS");
    if (nexus < 0 || !((gGame.mVisitedMaps >> nexus) & 1))
    {
        return;
    }
    const MapEntityList& list = kMapEntities[nexus];
    for (uint32_t i = 0; i < list.mCount; i++)
    {
        if (list.mEntities[i].mType == EPT_DungeonEntrance)
        {
            gGame.mReturnMap = nexus;
            gGame.mReturnX = list.mEntities[i].mX;
            gGame.mReturnY = list.mEntities[i].mY;
            return;
        }
    }
}

// placedAfter: the caller puts the player somewhere with World_PlaceAt straight after (which loads that screen).
static void LoadWorldMap(bool placedAfter = false)
{
    // PlayState.Warp: leaving a map whose boss is beaten.
    if (gGame.mMapName && gGame.mCurrentMap != static_cast<int>(gCurrentMap) &&
        ((gGame.mBossDefeated >> gGame.mCurrentMap) & 1))
    {
        gGame.mLeftAfterBoss |= 1u << gGame.mCurrentMap;
    }
    gGame.mCurrentMap = gCurrentMap;
    gGame.mMapName = kMapNames[gCurrentMap];
    gGame.mVisitedMaps |= 1u << gCurrentMap;
    if (gGame.mReturnMap != static_cast<int>(gCurrentMap) || strcmp(gGame.mMapName, "NEXUS") == 0)
    {
        SetReturnTargetToNexus();
    }
    // Entities first: World_Load places the player and loads the screen's entities.
    Entities_SetMap(kMapEntities[gCurrentMap], kEntitySheets, sizeof(kEntitySheets) / sizeof(kEntitySheets[0]),
                    kEntityTypeValues, sizeof(kEntityTypeValues) / sizeof(kEntityTypeValues[0]));
    World_Load(kMaps[gCurrentMap], kMapNames[gCurrentMap], kMapAudio[gCurrentMap], kMapScroll[gCurrentMap], kMapDoors[gCurrentMap],
               kMapDark[gCurrentMap], !placedAfter);
    World_SetSwapper(kMapSwapper[gCurrentMap]);
    Dialog_SetArea(kMapNames[gCurrentMap]);
}

// PlayState.Warp's map load, for the world's warps.
static void LoadWarpMap(uint8_t map)
{
    Mosaic_End(); // the old screen is gone: give back what the software blocks borrowed
    if (map != gCurrentMap)
    {
        gCurrentMap = map;
        LoadWorldMap(true);
    }
}

static void EnterMapViewerMode()
{
    REG_BG1CNT = 0;
    REG_BG2CNT = 0;
    REG_BG3CNT = 0;
    World_Enter();
    LoadWorldMap();
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

#ifdef ANODYNE_PERF
// The perf build's probe (tools/perf: an mGBA harness reads and writes it): each frame's CPU time, and warps on request.
struct PerfProbe final
{
    uint32_t mMagic;      // 'PERF'
    uint32_t mFrames;     // main loop frames
    uint32_t mLastWork;   // cycles from the VBlank to the loop's end, last frame
    uint32_t mMaxWork;    // the most since the harness last cleared it
    uint32_t mLagFrames;  // frames over one display frame (280896 cycles) since then
    uint32_t mMode;       // AppMode
    uint32_t mCellsW;     // the current map's size in 16x16 cells
    uint32_t mCellsH;
    uint32_t mMapCount;
    volatile uint32_t mCmdSeq; // the harness bumps it to warp to mCmdMap at (mCmdX, mCmdY)
    volatile int32_t mCmdMap;
    volatile int32_t mCmdX;
    volatile int32_t mCmdY;
    uint32_t mAckSeq;
    int32_t mMap; // where the game is now (a kMaps index), to check warps land
    uint32_t mEntities;
    uint32_t mAudioBad;     // AudioManager_SelfTest at boot: samples where qoa_run.s and the C++ loop differ
    uint32_t mAudioSamples; // and how many were compared
};
extern "C" PerfProbe gPerfProbe;
PerfProbe gPerfProbe __attribute__((section(".sbss")));
extern "C" uint32_t __IWRAM_END__[];

// Fills the free user stack with a pattern, so the harness can find the deepest it ever got.
__attribute__((noinline)) void PaintStack()
{
    uint32_t* const top = static_cast<uint32_t*>(__builtin_frame_address(0)) - 16;
    for (uint32_t* p = __IWRAM_END__; p < top; p++)
    {
        *p = 0x5354434B; // 'STCK'
    }
}
#endif

int main()
{
    // Standard cart ROM timing (3/1 waitstates + prefetch) instead of the
    // power-on 4/2 with no prefetch - everything still running from ROM
    // (Thumb code, const data, the audio blob) gets substantially faster.
    REG_WAITCNT = WS_STANDARD;
#ifdef ANODYNE_LINK
    Link_Init();
#endif
#ifdef ANODYNE_PERF
    PaintStack();
    {
        uint32_t bad = 0;
        uint32_t samples = 0;
        AudioManager_SelfTest(&bad, &samples);
        gPerfProbe.mAudioBad = bad;
        gPerfProbe.mAudioSamples = samples;
    }
#endif
    REG_DISPCNT = DCNT_MODE0; // no layers yet; keep the setup below black until the title takes over
    ScreenBlack(true);

    AudioManager_Init();
    TexDark_Init(); // (after the IRQ setup)
    // (The world starts the map's music itself, see World_Load.)

    // Start in the map viewer (audio is muted for now); START+SELECT still toggles to the audio test HUD.
    AppMode mode = AppMode::Title;
    bool modeComboWasDown = false;
    int blackFrames = 0; // frames left before ScreenBlack(false)
    int introPhase = 0;  // 0 wait, 1 dialogue, 2 wait
    int introTimer = 0;

    // Door/teleporter warps (PlayState S_MAP_EXIT / S_MAP_ENTER) run in World_Update; the screen shows their fade.
    bool warpShown = false;

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
    GameState_Reset();
    World_Init(objVram);
    World_SetMapLoader(LoadWarpMap);
    World_SetMapTable(kMaps, kMapCount);
    World_SetDarkTextures(kDarkTextures);
    Dialog_Init(objVram);
    Hud_Init(objVram);
    LoadWorldMap(); // again: the map's overlay sprites need the VRAM allocator
    bool galleryOn = false;
    int galleryPage = 0;
    GalleryLoadPage(objVram, galleryPage);
    // Hold L+R at boot for the pixelate test scene (compared against the C# one, see pixelate_test.hpp).
    key_poll();
    if (key_is_down(KEY_L) && key_is_down(KEY_R))
    {
        mode = AppMode::PixelateTest;
        ScreenBlack(false);
        PixelateTest_Enter();
    }
    else
    {
        Title_Enter(objVram);
    }

    while (1)
    {
        // The loop was running unsynced to the display (no vsync wait), so
        // the HUD's every-frame erase+redraw could land mid-scan and tear/
        // flicker - VBlankIntrWait() (relies on the VBlank ISR clearing the
        // BIOS's REG_IFBIOS flag, which tonc's irq_init/master dispatcher
        // already does) throttles this loop to once per real 60Hz frame.
        VBlankIntrWait();
#ifdef ANODYNE_PERF
        const uint32_t perfStart = AudioManager_Cycles();
        if (mode == AppMode::MapViewer && gPerfProbe.mCmdSeq != gPerfProbe.mAckSeq)
        {
            gPerfProbe.mAckSeq = gPerfProbe.mCmdSeq;
            gSettings.mInvincible = true;
            GameProbe_Heal();
            World_DropChildStates();
            GameProbe_Warp(kMapNames[gPerfProbe.mCmdMap % kMapCount], gPerfProbe.mCmdX, gPerfProbe.mCmdY);
        }
#endif
#ifdef ANODYNE_LINK
        // Driven over the link port: the host's keys, each frame they're held a fresh press (as the C# game sees the
        // bot's keys, and as the simulator gives them below).
        Link_OnFrame();
        key_poll();
        __key_curr = Link_Keys();
        __key_prev = 0;
#else
        key_poll();
#endif
#ifdef ANODYNE_SIM
        // Driven by the C# bot (SimHost): every frame a key is held is a fresh press, as the C# game sees the bot's keys.
        static const bool sKeyRepeat = getenv("ANODYNE_SIM_KEY_REPEAT") != nullptr;
        if (sKeyRepeat)
        {
            __key_prev = 0;
        }
#endif
        Game_CountFrame();
        {
            static const char* const kModeNames[] = { "audio_test", "play", "title", "intro", "pixelate_test", "credits" };
            gGame.mModeName = kModeNames[static_cast<int>(mode)];
        }

        // ------------------------------------------------------------
        // MODE SWITCH (hold START+SELECT together)
        // ------------------------------------------------------------
        bool modeComboDown = mode != AppMode::PixelateTest && key_is_down(KEY_START) && key_is_down(KEY_SELECT);
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

        if (mode == AppMode::PixelateTest)
        {
            PixelateTest_Update();
        }
        else if (mode == AppMode::AudioTest)
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
        else if (mode == AppMode::Title)
        {
            int slot = 0;
            const bool chosen = Title_Update(&slot);
            if (chosen && Save_Exists(slot))
            {
                // MainMenuState: a file with a save goes back to its checkpoint (GlobalState.LoadSave).
                ScreenBlack(true);
                Title_Exit(objVram);
                oam_init(oam_mem, 128);
                blackFrames = 2;
                GameState_Reset();
                Dialog_ResetProgress();
                Save_SetSlot(slot);
                Save_Read(slot);
                World_NewPlayState();
                gCurrentMap = gGame.mCheckpointMap >= 0 ? static_cast<uint32_t>(gGame.mCheckpointMap) : 0;
                mode = AppMode::MapViewer;
                EnterMapViewerMode();
                galleryOn = false;
                LoadWorldMap(true);
                World_PlaceAt(gGame.mCheckpointX, gGame.mCheckpointY, 0);
            }
            else if (chosen)
            {
                // A new game in this file.
                GameState_Reset();
                World_NewPlayState();
                Dialog_ResetProgress();
                Save_SetSlot(slot);
                ScreenBlack(true);
                Title_Exit(objVram);
                oam_init(oam_mem, 128);
                // IntroState: 1.5s of black, the sage's text in the box, 1.5s of black, then BLANK.
                REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D;
                REG_WININ = REG_WINOUT = 0;
                REG_BLDCNT = 0;
                REG_BLDY = 0;
                pal_bg_mem[0] = 0;
                ScreenBlack(false);
                mode = AppMode::Intro;
                introPhase = 0;
                introTimer = 90;
            }
        }
        else if (mode == AppMode::Credits)
        {
            if (Credits_Update())
            {
                Credits_Exit();
                oam_init(oam_mem, 128);
                mode = AppMode::Title;
                TexDark_Off();
                Title_Enter(objVram);
            }
        }
        else if (mode == AppMode::Intro)
        {
            bool startGame = false;
            if (introPhase == 0 || introPhase == 2)
            {
                if (--introTimer <= 0)
                {
                    if (introPhase == 0)
                    {
                        introPhase = 1;
                        Dialog_Open(Dialog_GetFor("sage", "BLANK", "intro", 0), false, true);
                    }
                    else
                    {
                        startGame = true;
                    }
                }
            }
            else if (!Dialog_Update())
            {
                introPhase = 2;
                introTimer = 90;
            }
            if (startGame)
            {
                ScreenBlack(true); // hide the swap to the world until its first frame is drawn
                blackFrames = 2;
                oam_init(oam_mem, 128);
                for (uint32_t i = 0; i < kMapCount; i++)
                {
                    if (strcmp(kMapNames[i], "BLANK") == 0)
                    {
                        gCurrentMap = i;
                    }
                }
                mode = AppMode::MapViewer;
                EnterMapViewerMode();
                galleryOn = false;
                LoadWorldMap(true);
                World_PlaceAt(77, 87, 0); // IntroState: the new game starts in BLANK at (77, 87)
                const int street = Game_MapIndex("STREET");
                if (street >= 0)
                {
                    gGame.mNexusPortals |= 1u << street; // ActivatedNexusPortals.Add("STREET")
                }
            }
        }
        else
        {
            // ------------------------------------------------------------
            // MAP CYCLING (SELECT alone - the combo above is caught before
            // key_hit() would see it as a single press; START is the pause menu)
            // ------------------------------------------------------------
            if (key_hit(KEY_SELECT) && !modeComboDown)
            {
                gCurrentMap = (gCurrentMap == 0) ? (kMapCount - 1) : (gCurrentMap - 1);
                ScreenBlack(true);
                LoadWorldMap();
                blackFrames = 2;
            }

            // B+A toggles the sprite gallery (full-screen, so the 160x160 playfield window is off meanwhile);
            // R/L change its page. (A alone is Accept in the world.)
            if (key_hit(KEY_A) && key_is_down(KEY_B))
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
                int warpDark = 0;
                int warpPix = 256;
                bool swapped = false; // (the loader gave back the old screen's mosaic blocks)
                if (World_GetWarpFade(warpDark, warpPix, swapped))
                {
                    World_ScreenDarken((warpDark * 16) >> 16);
                    MosaicBgOffsets offsets;
                    World_GetBgOffsets(offsets.x, offsets.y);
                    Mosaic_Apply(warpPix, offsets);
                    warpShown = true;
                }
                else if (warpShown)
                {
                    warpShown = false;
                    Mosaic_End();
                    World_ScreenDarken(0);
                }
                if (Credits_TakeRequest())
                {
                    warpShown = false;
                    mode = AppMode::Credits;
                    TexDark_Off();
                    Credits_Enter(objVram);
                }
                else if (World_TakeQuickLoad())
                {
                    // PlayState.QuickLoad: the quick save's progress, a new play state where it was made
                    int map = 0;
                    int x = 0;
                    int y = 0;
                    Pause_Close();
                    Dialog_Close();
                    GameState_Reset();
                    Dialog_ResetProgress();
                    Save_QuickRead(map, x, y);
                    World_NewPlayState();
                    warpShown = false;
                    Mosaic_End();
                    ScreenBlack(true);
                    blackFrames = 2;
                    gCurrentMap = static_cast<uint32_t>(map);
                    LoadWorldMap(true);
                    World_PlaceAt(x, y, 0);
                }
                else if (Game_TakeTitleRequest())
                {
                    // GameState.SetState<TitleState>: every effect is deactivated
                    Pause_Close();
                    Dialog_Close();
                    warpShown = false;
                    Ecs_FlushRenderCaches();
                    Mosaic_End();
                    Screen_StopWave();
                    Screen_EndEnding();
                    Screen_SetFadeWhite(false);
                    Screen_SetFade(0);
                    REG_BLDCNT = 0;
                    REG_BLDY = 0;
                    oam_init(oam_mem, 128);
                    mode = AppMode::Title;
                    TexDark_Off();
                    Title_Enter(objVram);
                }
            }
        }

        if (blackFrames > 0 && --blackFrames == 0)
        {
            ScreenBlack(false);
        }

        AudioManager_Update();
#ifdef ANODYNE_PERF
        {
            PerfProbe& p = gPerfProbe;
            p.mMagic = 0x46524550;
            p.mFrames++;
            p.mLastWork = AudioManager_Cycles() - perfStart;
            p.mMaxWork = p.mLastWork > p.mMaxWork ? p.mLastWork : p.mMaxWork;
            p.mLagFrames += p.mLastWork > 280896 ? 1 : 0;
            p.mMode = static_cast<uint32_t>(mode);
            p.mCellsW = World_CellsW();
            p.mCellsH = World_CellsH();
            p.mMapCount = kMapCount;
            p.mMap = gGame.mCurrentMap;
            p.mEntities = static_cast<uint32_t>(Ecs_Count());
        }
#endif
    }
}
