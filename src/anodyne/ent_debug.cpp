// The Debug World's things (AnodyneSharp Entities/Interactive/Npc/Debug).
#include "entity_classes.h"

#include <stdio.h>
#include <string.h>

#include "audio_manager.h"
#include "dialog.h"

namespace
{
bool IsPlayer(const Entity& e)
{
    return &e == World_Player();
}

void SeparateSelfFirst(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        Ecs_Separate(self, other);
    }
}

void SeparateOtherFirst(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        Ecs_Separate(other, self);
    }
}

// ---- Big_Door (Npc/Debug/Big_Door.cs) -------------------------------------------------------------------------------
// Step on its sill: the card count lock (LockedScene). Off it and on again: it opens (OpenCutscene) and fades away.
// The cutscenes' squares are DrawAsUI entities the CutsceneState updates after each step of its coroutine; here they
// are plain data updated at the end of each step, with an entity apiece to draw them (UI coordinates: the C#'s 20px
// header sits above the playfield).
constexpr int kLockSquares = 36;
constexpr int kRotSquares = 20;
constexpr int kRandSquares = 36;

enum BigDoorStep : uint8_t
{
    BDS_WAIT_SILL,
    BDS_WAIT_OFF,
    BDS_WAIT_ON,
    BDS_WAIT_OPENED,
    BDS_FADE,
    BDS_DONE,
};

struct BigDoorData final
{
    uint8_t mStep;    // BigDoorStep
    bool mFinished;   // OpenCutscene is over
    Entity* mRegion;  // active_region
};

enum SquareState : uint8_t
{
    SQ_NONE,
    SQ_APPEAR,
    SQ_DISAPPEAR,
};

struct Square final
{
    fx mX;
    fx mY;
    fx mOpacity;
    fx mScale;
    fx mRot;
    fx mAngVel;
    fx mAngAcc;
    fx mAngle;  // RotatingOpenSquare.rotate_angle
    fx mGrowth; // RotatingOpenSquare.alpha_growth
    uint8_t mState;
    bool mExists;
    Entity* mDraw;
};

// CoroutineUtils.OnceEvery(CoroutineUtils.ForEach(squares, ...), tm): sets one square's state, then waits tm.
struct OnceEvery final
{
    int mNext;
    bool mWaiting;
    fx mT;
};

struct BigDoorScene final
{
    uint8_t mStep;
    Square mSquares[kLockSquares]; // LockSquare, RotatingOpenSquare or RandomOpenSquare
    int mCount;
    OnceEvery mSpawn;
    uint8_t mSpawnState; // what mSpawn gives each square
    fx mScoreX[2];
    fx mScoreOpacity[2];
    bool mLabels;
    // RotatingOpenSquare's statics
    fx mRadius;
    fx mSpeed;
    bool mGrow;
    // RandomOpenSquare's
    bool mGo;
    fx mTargetX;
    fx mTargetY;
};
static_assert(kRandSquares <= kLockSquares && kRotSquares <= kLockSquares, "one array for the three kinds");

BigDoorScene gScene __attribute__((section(".sbss")));

const EntityClass kUiSquare = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

bool OnceEveryNext(OnceEvery& o)
{
    if (o.mWaiting)
    {
        if (!MoveTo(o.mT, FxF(0.06f), kFxOne))
        {
            return true;
        }
        o.mWaiting = false;
    }
    if (o.mNext >= gScene.mCount)
    {
        return false;
    }
    gScene.mSquares[o.mNext++].mState = gScene.mSpawnState;
    o.mWaiting = true;
    o.mT = 0;
    return true;
}

void StartSpawning(uint8_t state)
{
    gScene.mSpawn = OnceEvery();
    gScene.mSpawnState = state;
}

void MakeSquares(int count)
{
    gScene.mCount = count;
    for (int i = 0; i < count; i++)
    {
        Square& s = gScene.mSquares[i];
        s = Square();
        s.mScale = kFxOne;
        s.mExists = true;
        s.mDraw = Ecs_Create(kUiSquare, 0, 0, PART_VISUAL);
        if (s.mDraw)
        {
            Ecs_SetSolid(*s.mDraw, false);
            Ecs_SetSheet(*s.mDraw, "solid_square");
            s.mDraw->mV->mLayer = LAYER_UI;
        }
    }
}

void DropSquare(Square& s)
{
    s.mExists = false;
    if (s.mDraw)
    {
        s.mDraw->mExists = false;
        s.mDraw = nullptr;
    }
}

void DropSquares()
{
    for (int i = 0; i < gScene.mCount; i++)
    {
        DropSquare(gScene.mSquares[i]);
    }
    gScene.mCount = 0;
}

// The squares' and labels' looks, after their updates.
void PlaceSquares()
{
    const fx left = Fx(gGame.mGridX * 160);
    const fx top = Fx(gGame.mGridY * 160 - 20);
    for (int i = 0; i < gScene.mCount; i++)
    {
        Square& s = gScene.mSquares[i];
        if (!s.mDraw)
        {
            continue;
        }
        // (the SolidColorRenderer scales about its middle: the port's sprites do too)
        s.mDraw->mT->mX = left + s.mX;
        s.mDraw->mT->mY = top + s.mY;
        s.mDraw->mT->mRot = s.mRot;
        s.mDraw->mV->mOpacity = s.mOpacity;
        s.mDraw->mV->mScale = s.mScale;
    }
    if (!gScene.mLabels)
    {
        return;
    }
    // UILabel(centerText): each line centred on the screen, then moved by the label's x.
    char count[8];
    snprintf(count, sizeof(count), "%d", Inventory_CardCount());
    const char* lines[3] = { count, "---", "20" };
    int xs[3];
    int ys[3] = { 10, 20, 30 };
    int n = 0;
    for (int i = 0; i < 3; i++)
    {
        const int label = i < 2 ? 0 : 1;
        if (gScene.mScoreOpacity[label] < FxF(0.5f))
        {
            continue;
        }
        int len = 0;
        while (lines[i][len])
        {
            len++;
        }
        lines[n] = lines[i];
        xs[n] = FxTrunc(gScene.mScoreX[label]) + 80 - len * 7 / 2;
        ys[n] = ys[i];
        n++;
    }
    if (n == 0)
    {
        Dialog_HideLabels();
        return;
    }
    Dialog_ShowLabels(lines, xs, ys, n, 10, 0, -1, true);
}

void EndScene()
{
    DropSquares();
    if (gScene.mLabels)
    {
        Dialog_HideLabels();
        gScene.mLabels = false;
    }
}

// LockSquare.Update: base.Update (the spin), then its coroutine.
void UpdateLockSquares()
{
    for (int i = 0; i < gScene.mCount; i++)
    {
        Square& s = gScene.mSquares[i];
        s.mAngVel += FxStep(s.mAngAcc);
        s.mRot += FxStep(s.mAngVel);
        if (s.mState == SQ_APPEAR)
        {
            const bool opaque = MoveTo(s.mOpacity, kFxOne, FxF(1.8f));
            const bool small = MoveTo(s.mScale, kFxOne, FxF(2.4f));
            if (opaque || small)
            {
                s.mState = SQ_NONE;
            }
        }
        else if (s.mState == SQ_DISAPPEAR && s.mExists)
        {
            s.mAngAcc = FxMul(FxF(1.67f), kFxPi);
            if (!MoveTo(s.mOpacity, 0, FxF(1.8f)))
            {
                MoveTo(s.mScale, FxF(0.2f), FxF(0.6f));
            }
            else
            {
                DropSquare(s);
                s.mState = SQ_NONE;
            }
        }
    }
    PlaceSquares();
}

// LockedScene: the squares (one per card, black for the ones held) and the score labels fly in, a flash, they go.
bool LockedScene(Entity&)
{
    BigDoorScene& sc = gScene;
    switch (sc.mStep)
    {
    case 0:
    {
        Game_PlaySfx("big_door_locked");
        MakeSquares(kLockSquares);
        const int cards = Inventory_CardCount();
        for (int i = 0; i < kLockSquares; i++)
        {
            Square& s = sc.mSquares[i];
            s.mX = Fx(10 + 24 * (i % 6));
            s.mY = Fx(20 + 24 * (i / 6));
            s.mScale = Fx(2);
            s.mOpacity = 0;
            if (s.mDraw)
            {
                Ecs_SetFrame(*s.mDraw, i < cards ? 0 : 1);
            }
        }
        sc.mScoreX[0] = -Fx(100);
        sc.mScoreX[1] = Fx(100);
        sc.mScoreOpacity[0] = sc.mScoreOpacity[1] = kFxOne;
        sc.mLabels = true;
        StartSpawning(SQ_APPEAR);
        sc.mStep = 1;
        UpdateLockSquares();
        return false;
    }
    case 1:
    {
        const bool done1 = MoveTo(sc.mScoreX[0], 0, Fx(240));
        const bool done2 = MoveTo(sc.mScoreX[1], 0, Fx(240));
        const bool spawning = OnceEveryNext(sc.mSpawn);
        if (!(done1 && done2 && !spawning && sc.mSquares[kLockSquares - 1].mState == SQ_NONE))
        {
            UpdateLockSquares();
            return false;
        }
        Screen_Flash(FxF(0.33f), FLASH_WHITE);
        sc.mStep = 10;
        [[fallthrough]];
    }
    case 10:
    {
        if (!Screen_FlashFull())
        {
            UpdateLockSquares();
            return false;
        }
        StartSpawning(SQ_DISAPPEAR);
        sc.mStep = 2;
        [[fallthrough]];
    }
    default:
    {
        MoveTo(sc.mScoreX[0], Fx(100), Fx(180));
        MoveTo(sc.mScoreX[1], -Fx(100), Fx(180));
        const bool gone1 = MoveTo(sc.mScoreOpacity[0], 0, FxF(1.2f));
        const bool gone2 = MoveTo(sc.mScoreOpacity[1], 0, FxF(1.2f));
        OnceEveryNext(sc.mSpawn);
        if (gone1 && gone2 && !sc.mSquares[kLockSquares - 1].mExists)
        {
            EndScene();
            return true;
        }
        UpdateLockSquares();
        return false;
    }
    }
}

// RotatingOpenSquare.Update / RandomOpenSquare.Update.
void UpdateOpenSquares(bool random)
{
    for (int i = 0; i < gScene.mCount; i++)
    {
        Square& s = gScene.mSquares[i];
        s.mRot += FxStep(s.mAngVel);
        if (!random)
        {
            if (gScene.mGrow)
            {
                MoveTo(s.mOpacity, kFxOne, s.mGrowth);
            }
            s.mAngle += FxStep(gScene.mSpeed);
            s.mX = Fx(72) + FxMul(FxCos(s.mAngle), gScene.mRadius);
            s.mY = Fx(72) + FxMul(FxSin(s.mAngle), gScene.mRadius);
        }
        else if (gScene.mGo && s.mExists)
        {
            const bool atX = MoveTo(s.mX, gScene.mTargetX, Fx(60));
            const bool atY = MoveTo(s.mY, gScene.mTargetY, Fx(60));
            if (atX && atY)
            {
                DropSquare(s);
            }
        }
    }
    PlaceSquares();
}

// OpenCutscene: twenty squares spin in and out, a long flash, then squares scattered at random fly to the door.
bool OpenCutscene(Entity& self)
{
    BigDoorScene& sc = gScene;
    switch (sc.mStep)
    {
    case 0:
        sc.mGrow = true;
        sc.mRadius = Fx(45);
        sc.mSpeed = FxF(1.8f);
        MakeSquares(kRotSquares);
        for (int i = 0; i < kRotSquares; i++)
        {
            Square& s = sc.mSquares[i];
            const fx percentage = fx(static_cast<float>(i + 1) / 20.0f);
            s.mAngle = percentage * 360; // (degrees taken for radians, for a fun spread)
            s.mGrowth = FxMul(percentage, FxF(0.48f));
            s.mOpacity = 0;
        }
        sc.mStep = 1;
        UpdateOpenSquares(false);
        return false;
    case 1:
        if (sc.mSquares[kRotSquares - 1].mOpacity < FxF(0.8f))
        {
            UpdateOpenSquares(false);
            return false;
        }
        sc.mGrow = false;
        sc.mSpeed = FxF(3.6f);
        sc.mStep = 2;
        [[fallthrough]];
    case 2:
        if (!MoveTo(sc.mRadius, -Fx(100), Fx(84)))
        {
            UpdateOpenSquares(false);
            return false;
        }
        sc.mSpeed = FxF(5.4f);
        sc.mStep = 3;
        [[fallthrough]];
    case 3:
    {
        if (!MoveTo(sc.mRadius, 0, Fx(84)))
        {
            UpdateOpenSquares(false);
            return false;
        }
        Screen_Flash(FxF(1.6f), FLASH_WHITE);
        sc.mStep = 30;
        [[fallthrough]];
    }
    case 30:
    {
        if (!Screen_FlashFull())
        {
            UpdateOpenSquares(false);
            return false;
        }
        DropSquares();
        // MapUtilities.GetInGridPosition(Position) + width / 2, taken as UI coordinates
        sc.mTargetX = self.mT->mX - Fx(gGame.mGridX * 160) + Fx(16);
        sc.mTargetY = self.mT->mY - Fx(gGame.mGridY * 160);
        sc.mGo = false;
        MakeSquares(kRandSquares);
        for (int i = 0; i < kRandSquares; i++)
        {
            Square& s = sc.mSquares[i];
            s.mScale = Rng_Unit() * 2 + FxF(0.5f);
            s.mX = Fx(Rng_Range(0, 160));
            s.mY = Fx(Rng_Range(0, 160));
            s.mAngVel = FxMul(Rng_Unit(), FxF(6.2831855f)) * 2 - FxF(6.2831855f);
            s.mOpacity = Rng_Unit();
        }
        sc.mStep = 4;
        UpdateOpenSquares(true);
        return false;
    }
    case 4:
        if (Screen_FlashActive())
        {
            UpdateOpenSquares(true);
            return false;
        }
        sc.mGo = true;
        sc.mStep = 5;
        [[fallthrough]];
    default:
        for (int i = 0; i < sc.mCount; i++)
        {
            if (sc.mSquares[i].mExists)
            {
                UpdateOpenSquares(true);
                return false;
            }
        }
        EndScene();
        self.Data<BigDoorData>().mFinished = true;
        return true;
    }
}

void StartScene(Entity& self, CutsceneFn fn)
{
    EndScene();
    gScene = BigDoorScene();
    Game_StartCutscene(self, fn);
}

void BigDoorUpdate(Entity& self)
{
    BigDoorData& d = self.Data<BigDoorData>();
    Ecs_BaseUpdate(self);
    const bool on = d.mRegion && Ecs_Intersects(Ecs_Hitbox(*World_Player()), Ecs_Hitbox(*d.mRegion));
    switch (d.mStep)
    {
    case BDS_WAIT_SILL:
        if (!on)
        {
            break;
        }
        StartScene(self, LockedScene);
        d.mStep = BDS_WAIT_OFF;
        [[fallthrough]];
    case BDS_WAIT_OFF:
        if (on)
        {
            break;
        }
        d.mStep = BDS_WAIT_ON;
        [[fallthrough]];
    case BDS_WAIT_ON:
        if (!on)
        {
            break;
        }
        StartScene(self, OpenCutscene);
        d.mStep = BDS_WAIT_OPENED;
        [[fallthrough]];
    case BDS_WAIT_OPENED:
        if (!d.mFinished)
        {
            break;
        }
        d.mStep = BDS_FADE;
        [[fallthrough]];
    case BDS_FADE:
        if (!MoveTo(self.mV->mOpacity, 0, FxF(0.6f)))
        {
            break;
        }
        self.mExists = false;
        d.mStep = BDS_DONE;
        break;
    default:
        break;
    }
}

const EntityClass kBigDoor = {
    "Big_Door", BigDoorUpdate, nullptr, SeparateSelfFirst, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER,  0,
};

const EntityClass kBigDoorRegion = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- TestNPC (Npc/Debug/TestNPC.cs), DevEaster (Npc/DevEaster.cs), BigTree (Decorations/BigTree.cs) ------------------
bool TestNpcInteract(Entity&, Facing)
{
    Game_ShowDialogue("test", "scene_1");
    return true;
}

const EntityClass kTestNpc = {
    "TestNPC", nullptr, nullptr, SeparateSelfFirst, TestNpcInteract, nullptr, nullptr, nullptr,
    0,         GROUP_PLAYER, ECF_INTERACTABLE,
};

bool DevEasterInteract(Entity& self, Facing)
{
    Game_ShowDialogue("generic_npc", self.mPreset->mFrame == 17 ? "melos" : "marina");
    return true;
}

const EntityClass kDevEaster = {
    "DevEaster", nullptr, nullptr, SeparateOtherFirst, DevEasterInteract, nullptr, nullptr, nullptr,
    0,           GROUP_PLAYER, ECF_INTERACTABLE,
};

const EntityClass kBigTree = {
    "BigTree", nullptr, nullptr, SeparateSelfFirst, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- SoundTestConsole (Gadget/SoundTestConsole.cs) ------------------------------------------------------------------
// Its SoundTest menu: a ListSubstate (Muzak / SFX) whose two pages pick a song or a sound with Left and Right.
enum JukeboxState : uint8_t
{
    JUKE_MENU,
    JUKE_MUSIC,
    JUKE_SFX,
};

const char* const kSongOptions[] = {
    "Title",
    "Blank", "Nexus", "Street",
    "Overworld", "Bedroom", "Bedroom Boss",
    "Mitra", "Fields",
    "Beach", "Redsea", "Sacrificial",
    "Forest", "Cliff",
    "Redcave", "Redcave Boss",
    "Crowd", "Crowd Boss",
    "Windmill", "Suburb",
    "Space", "Apartment", "Apartment Boss",
    "Roof", "Hotel", "Hotel Boss",
    "Cell", "Circus", "Circus Boss",
    "Pre-Terminal", "Sagefight", "Terminal",
    "Go", "Happy Init", "Happy", "Blue",
    "Briar Fight", "Ending",
    "Gameover", "Trailer",
};
constexpr int kSongOptionCount = sizeof(kSongOptions) / sizeof(kSongOptions[0]);

struct SoundTestData final
{
    uint8_t mJukebox; // JukeboxState
    int8_t mState;    // ListSubstate.state
    int8_t mLastState;
    int16_t mSelection;
    bool mPlaySfx;
    bool mSelected; // _selectedOption: an ActionOption, done (Exit) as soon as it is chosen
    bool mExit;
};

int SoundOptionCount(const SoundTestData& d)
{
    return d.mJukebox == JUKE_MUSIC ? kSongOptionCount : static_cast<int>(AudioManager_GetSfxCount());
}

void SoundTestSetLabels(SoundTestData& d)
{
    d.mState = 0;
    switch (d.mJukebox)
    {
    case JUKE_MENU:
        d.mPlaySfx = true;
        d.mSelection = -1;
        break;
    case JUKE_MUSIC:
        d.mSelection = 0;
        break;
    default:
        d.mPlaySfx = false;
        d.mSelection = 0;
        break;
    }
}

// SoundTest.DoMusic: the label as a song name.
void SoundTestPlaySong(const char* option)
{
    static char song[24];
    size_t n = 0;
    for (; option[n] && n + 1 < sizeof(song); n++)
    {
        const char c = option[n] == ' ' ? '-' : option[n];
        song[n] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    song[n] = '\0';
    if (strcmp(song, "happy-init") == 0)
    {
        strcpy(song, "happy_alt");
    }
    else if (strcmp(song, "pre-terminal") == 0)
    {
        strcpy(song, "terminal_alt");
    }
    else if (strcmp(song, "roof") == 0)
    {
        strcpy(song, "hotel_alt");
    }
    else if (strcmp(song, "crowd-boss") == 0)
    {
        strcpy(song, "crowd_boss");
    }
    else if (strcmp(song, "trailer") == 0)
    {
        strcpy(song, "anodyne_TRAILER_1");
    }
    Game_PlaySong(song);
}

// ListSubstate.Select: the option's action, then the select sound.
void SoundTestSelect(SoundTestData& d)
{
    d.mSelected = true;
    switch (d.mJukebox)
    {
    case JUKE_MENU:
        d.mJukebox = d.mState == 0 ? JUKE_MUSIC : JUKE_SFX;
        SoundTestSetLabels(d);
        break;
    case JUKE_MUSIC:
        SoundTestPlaySong(kSongOptions[d.mSelection]);
        break;
    default:
        if (d.mSelection < static_cast<int>(AudioManager_GetSfxCount()))
        {
            Game_PlaySfx(AudioManager_GetSfxName(static_cast<uint32_t>(d.mSelection)));
        }
        break;
    }
    if (d.mPlaySfx)
    {
        Game_PlaySfx("menu_select");
    }
}

void SoundTestDraw(const SoundTestData& d)
{
    // The dialogue box at (2, 85) of the screen, which is (2, 65) of the playfield; the title at (3, 1) of it, the
    // options a line (font line + 4) apart.
    constexpr int kX = 2;
    constexpr int kY = 65;
    constexpr int kLine = 12;
    static char option[32];
    const char* lines[3];
    int xs[3];
    int ys[3];
    int count = 0;
    int selX = -1;
    int selY = -1;
    lines[0] = d.mJukebox == JUKE_MENU ? "Jukebox!" : (d.mJukebox == JUKE_MUSIC ? "MUSIC" : "SFX");
    xs[0] = kX + 3;
    ys[0] = kY + 1;
    count = 1;
    if (d.mJukebox == JUKE_MENU)
    {
        lines[1] = "Muzak";
        xs[1] = kX + 16;
        ys[1] = kY + kLine;
        lines[2] = "SFX";
        xs[2] = kX + 20;
        ys[2] = kY + kLine * 2;
        count = 3;
        if (!d.mSelected)
        {
            selX = xs[1 + d.mState] - 7;
            selY = ys[1 + d.mState] + 2;
        }
    }
    else
    {
        const char* name = d.mJukebox == JUKE_MUSIC ? kSongOptions[d.mSelection]
                                                    : AudioManager_GetSfxName(static_cast<uint32_t>(d.mSelection));
        size_t n = 0;
        for (; name && name[n] && n + 1 < sizeof(option); n++)
        {
            option[n] = name[n] == '_' ? ' ' : name[n];
        }
        option[n] = '\0';
        lines[1] = option;
        xs[1] = kX + 3;
        ys[1] = kY + kLine;
        count = 2;
    }
    Dialog_ShowPanel(kX, kY, 156, 29);
    Dialog_ShowLabels(lines, xs, ys, count, kY, selX, selY, true);
}

// The SoundTest child state's frame; true once it exits.
bool SoundTestStep(Entity& self)
{
    SoundTestData& d = self.Data<SoundTestData>();
    if (d.mJukebox == JUKE_MENU)
    {
        // ListSubstate.HandleInput
        if (d.mSelected)
        {
            d.mSelected = false;
            d.mState = d.mLastState;
        }
        else if (key_hit(KEY_UP))
        {
            if (d.mState > 0)
            {
                d.mState--;
            }
        }
        else if (key_hit(KEY_DOWN))
        {
            if (d.mState < 1)
            {
                d.mState++;
            }
        }
        else if (key_hit(KEY_A))
        {
            SoundTestSelect(d);
        }
        else if (key_hit(KEY_B))
        {
            d.mExit = true; // ExitSubState
            Game_PlaySfx("menu_select");
        }
    }
    else
    {
        const int count = SoundOptionCount(d);
        if (key_hit(KEY_LEFT))
        {
            d.mSelection = static_cast<int16_t>(d.mSelection == 0 ? count - 1 : d.mSelection - 1);
        }
        else if (key_hit(KEY_RIGHT))
        {
            d.mSelection = static_cast<int16_t>(d.mSelection == count - 1 ? 0 : d.mSelection + 1);
        }
        else if (key_hit(KEY_A))
        {
            SoundTestSelect(d);
        }
        else if (key_hit(KEY_B))
        {
            d.mJukebox = JUKE_MENU;
            SoundTestSetLabels(d);
        }
    }
    // ListSubstate.Update
    if (d.mLastState != d.mState)
    {
        d.mLastState = d.mState;
        Game_PlaySfx("menu_move");
    }
    if (d.mExit)
    {
        Dialog_HideLabels();
        Dialog_HidePanel();
        return true;
    }
    SoundTestDraw(d);
    return false;
}

void SoundTestCollided(Entity& self, Entity& other)
{
    Ecs_Separate(other, self);
}

bool SoundTestInteract(Entity& self, Facing)
{
    SoundTestData& d = self.Data<SoundTestData>();
    d.mExit = false;
    d.mLastState = d.mState; // GetControl
    Game_StartCutscene(self, SoundTestStep);
    SoundTestDraw(d);
    return false;
}

const EntityClass kSoundTestConsole = {
    "SoundTestConsole", Ecs_BaseUpdate, nullptr, SoundTestCollided, SoundTestInteract, nullptr, nullptr, nullptr,
    0,                  GROUP_PLAYER,   ECF_INTERACTABLE,
};

// ---- Black_Thing (Npc/Debug/Black_Thing.cs) -------------------------------------------------------------------------
// Jump in it 50 times: its squares grow huge, and it counts as a puzzle solved.
struct BlackThingData final
{
    int mJumps;
    Entity* mThings[3]; // BouncingBlackThing x2, RotatingBlackThing
};

struct ThingData final
{
    fx mMinScale;
    fx mMaxScale;
    fx mRealScale; // BouncingBlackThing
    fx mTime;      // RotatingBlackThing.curTime
    bool mGrowing;
};

void BlackThingUpdate(Entity& self)
{
    BlackThingData& d = self.Data<BlackThingData>();
    if (!Ecs_Intersects(Ecs_Hitbox(self), Ecs_Hitbox(*World_Player())) || !key_hit(KEY_B))
    {
        return;
    }
    Game_PlaySfx("fall_in_hole");
    if (++d.mJumps != 50)
    {
        return;
    }
    const float maxScales[3] = { 10, 12, 9 };
    for (int i = 0; i < 3; i++)
    {
        if (d.mThings[i])
        {
            d.mThings[i]->Data<ThingData>().mMaxScale = fx(maxScales[i]);
        }
    }
    gGame.mPuzzlesSolved++;
}

// (the C#'s "force centered rendering": an even number of pixels wide)
fx EvenScale(fx scale)
{
    const int evenWidth = static_cast<int>(FxMul(scale, Fx(32)).mV / 2) * 2;
    return fx(static_cast<float>(evenWidth) / 32.0f);
}

void BouncingThingUpdate(Entity& self)
{
    ThingData& d = self.Data<ThingData>();
    if (MoveTo(d.mRealScale, d.mGrowing ? d.mMaxScale : d.mMinScale, FxF(0.03f * 30)))
    {
        d.mGrowing = !d.mGrowing;
    }
    self.mV->mScale = EvenScale(d.mRealScale);
}

void RotatingThingUpdate(Entity& self)
{
    ThingData& d = self.Data<ThingData>();
    if (MoveTo(d.mTime, kFxOne, FxF(0.5f)))
    {
        d.mTime = 0;
    }
    const fx lerp = (kFxOne + FxSin(FxMul(d.mTime, FxF(6.2831855f)))) / 2;
    self.mV->mScale = EvenScale(FxMul(lerp, d.mMaxScale - d.mMinScale) + d.mMinScale);
    self.mT->mRot = FxMul(lerp, kFxPi) - kFxPi;
}

const EntityClass kBlackThing = {
    "Black_Thing", BlackThingUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBouncingThing = {
    "BouncingBlackThing", BouncingThingUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kRotatingThing = {
    "RotatingBlackThing", RotatingThingUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* MakeThing(const EntityClass& cls, const Entity& parent, int frame, float opacity)
{
    Entity* e = Ecs_Create(cls, parent.mT->mX, parent.mT->mY, PART_VISUAL | PART_BRAIN);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 32;
    e->mB->mH = 32;
    Ecs_SetSolid(*e, false);
    Ecs_SetSheet(*e, "solid_32");
    Ecs_SetFrame(*e, frame);
    e->mV->mOpacity = fx(opacity);
    e->Data<ThingData>() = ThingData();
    return e;
}
} // namespace

Entity* Spawn_BigDoor(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBigDoor, preset, "big_door", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 32;
    e->mB->mH = 32;
    e->mB->mImmovable = true;
    BigDoorData& d = e->Data<BigDoorData>();
    d = BigDoorData();
    Entity* r = Ecs_Create(kBigDoorRegion, e->mT->mX + Fx(15), e->mT->mY + Fx(34), PART_VISUAL); // (it stays when the door goes)
    if (r)
    {
        r->mB->mW = 2;
        r->mB->mH = 2;
        Ecs_SetSolid(*r, false);
        Ecs_SetSheet(*r, "solid_dot");
        d.mRegion = r;
    }
    return e;
}

Entity* Spawn_TestNpc(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kTestNpc, preset, "npc");
    if (e)
    {
        e->mB->mImmovable = true;
    }
    return e;
}

Entity* Spawn_DevEaster(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kDevEaster, preset, "dev_npcs");
    if (e)
    {
        e->mB->mImmovable = true;
        Ecs_SetFrame(*e, preset.mFrame == 17 ? 0 : 10);
    }
    return e;
}

Entity* Spawn_BigTree(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBigTree, preset, "TREE", 64, 64);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mOffX = Fx(16);
    e->mV->mOffY = Fx(32);
    e->mB->mW = 32;
    e->mB->mH = 32;
    e->mT->mX += Fx(16);
    e->mT->mY += Fx(32);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_SoundTestConsole(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSoundTestConsole, preset, "console");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_Play(*e, "spaz");
    SoundTestData& d = e->Data<SoundTestData>();
    d = SoundTestData();
    SoundTestSetLabels(d);
    return e;
}

Entity* Spawn_BlackThing(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBlackThing, preset, "solid_32", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 32;
    e->mB->mH = 32;
    Ecs_SetSolid(*e, false);
    e->mV->mOpacity = FxF(0.3f);
    BlackThingData& d = e->Data<BlackThingData>();
    d = BlackThingData();
    // (Color.Red * 0.25f, Color.Green * 0.5f: premultiplied, so dimmer as well as see-through)
    const float min[3] = { 0.5f, 0.5f, 0.75f };
    const float max[3] = { 2, 2, 3 };
    const float cur[3] = { 0.5f, 2, 0 };
    const EntityClass* classes[3] = { &kBouncingThing, &kBouncingThing, &kRotatingThing };
    const int frames[3] = { 1, 2, 3 };
    const float opacities[3] = { 0.25f, 0.5f, 0.7f };
    for (int i = 0; i < 3; i++)
    {
        Entity* t = MakeThing(*classes[i], *e, frames[i], opacities[i]);
        if (t)
        {
            ThingData& td = t->Data<ThingData>();
            td.mMinScale = fx(min[i]);
            td.mMaxScale = fx(max[i]);
            td.mRealScale = fx(cur[i]);
            td.mGrowing = true;
        }
        d.mThings[i] = t;
    }
    return e;
}
