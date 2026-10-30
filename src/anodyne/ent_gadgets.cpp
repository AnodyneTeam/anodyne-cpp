// Gadgets and events (AnodyneSharp Entities/Gadget, Entities/Events).
#include "entity_classes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dialog.h"
#include "entities.h"
#include "pause_draw.h"
#include "player.h"

namespace
{
// ---- Console (Console.cs) -------------------------------------------------------------------------------------
void ConsoleCollided(Entity& self, Entity& other)
{
    Ecs_Separate(other, self);
}

bool ConsoleInteract(Entity& self, Facing)
{
    if (Preset_Activated(*self.mPreset))
    {
        return false;
    }
    Preset_SetActivated(*self.mPreset, true);
    Ecs_Play(self, "green");
    gGame.mPuzzlesSolved++;
    Game_PlaySfx("get_small_health");
    return true;
}

const EntityClass kConsole = {
    "Console", nullptr, nullptr, ConsoleCollided, ConsoleInteract, nullptr, nullptr, nullptr,
    0,         GROUP_PLAYER, ECF_INTERACTABLE,
};

// ---- Gate (Gate.cs) -------------------------------------------------------------------------------------------
struct GateData final
{
    bool mHeldDown; // the player stood on it when the screen was entered
};

bool GateCondition(const MapEntity& p)
{
    // 0-4, 10-14: enemies killed; 5-7, 15-17: puzzles solved
    const int check = (p.mFrame % 10 >= 5) ? gGame.mPuzzlesSolved : gGame.mEnemiesKilled;
    return check > p.mFrame % 5;
}

void GateUpdate(Entity& self)
{
    GateData& d = self.Data<GateData>();
    const MapEntity& p = *self.mPreset;
    if (d.mHeldDown)
    {
        if (!Ecs_Intersects(Ecs_Hitbox(*World_Player()), Ecs_Hitbox(self)))
        {
            d.mHeldDown = false;
            Ecs_Play(self, "close");
            Game_PlaySfx("hit_ground_1");
            Ecs_SetSolid(self, true);
        }
    }
    else if (Ecs_AnimIs(self, "still") || Ecs_AnimIs(self, "close"))
    {
        if (GateCondition(p))
        {
            Ecs_Play(self, "open");
            Game_PlaySfx("open");
            if (p.mFrame < 10)
            {
                Preset_SetAlive(p, false);
            }
            Ecs_SetSolid(self, false);
        }
    }
    Ecs_BaseUpdate(self);
}

void GateCollided(Entity& self, Entity& other)
{
    if (Ecs_Solid(self))
    {
        Ecs_Separate(self, other);
    }
}

const EntityClass kGate = {
    "Gate", GateUpdate, nullptr, GateCollided, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_PART_OF_MAP,
};

// ---- SageBlank (Sage.cs): BLANK's invisible Sage talks when the camera arrives on its screen ---------------------
void SageBlankEvent(Entity& self, int ev)
{
    if (ev != GEV_END_SCREEN_TRANSITION || !self.mExists)
    {
        return;
    }
    Game_ShowDialogue("sage", "intro", self.mPreset->mFrame + 1);
    self.mExists = false;
    Preset_SetAlive(*self.mPreset, false);
}

const EntityClass kSageBlank = {
    "SageBlank", nullptr, nullptr, nullptr, nullptr, nullptr, SageBlankEvent, nullptr, 0, 0, 0,
};

// ---- TreasureChest (TreasureChest.cs) and its treasures (Treasures/*.cs) ----------------------------------------
enum TreasureKind : uint8_t
{
    TK_BROOM,
    TK_WIDE,
    TK_LONG,
    TK_SWAP,
    TK_KEY,
    TK_CARD,
    TK_SECRET,
    TK_FAILSAFE,
};

struct TreasureData final
{
    uint8_t mKind;
    uint8_t mCardState; // CardTreasure.AnimState
    fx mEndY;
    fx mTimer;
};

// Treasure.Update: rises 16 px while it flickers, then goes.
void TreasureUpdate(Entity& self)
{
    TreasureData& d = self.Data<TreasureData>();
    Ecs_BaseUpdate(self);
    if (d.mKind == TK_CARD)
    {
        // CardTreasure.GrowthAnim
        Transform& t = *self.mT;
        Visual& v = *self.mV;
        const fx gridMiddle = Fx(gGame.mGridY * 160 + 80);
        switch (d.mCardState)
        {
        case 0:
            d.mCardState++;
            t.mVelY = -Fx(24);
            if (t.mY <= gridMiddle)
            {
                t.mVelY = -t.mVelY;
            }
            break;
        case 1:
            v.mScale += FxStep(FxF(0.5f));
            if (v.mScale >= kFxOne)
            {
                v.mScale = kFxOne;
                t.mVelX = t.mVelY = 0;
                d.mCardState++;
            }
            break;
        case 2:
            d.mTimer += kFxTick;
            if (d.mTimer > FxSec(1.0f))
            {
                d.mCardState++;
                t.mAccY = -Fx(80);
                t.mAngVel = FxF(50.0f * 3.14159265f / 180.0f);
                t.mAngAcc = FxF(200.0f * 3.14159265f / 180.0f);
                if (t.mY <= gridMiddle)
                {
                    t.mAccY = -t.mAccY;
                    t.mAngVel = -t.mAngVel;
                    t.mAngAcc = -t.mAngAcc;
                }
            }
            break;
        case 3:
            v.mScale += FxStep(FxF(3.0f));
            v.mOpacity -= FxStep(FxF(0.5f));
            if (v.mOpacity <= 0)
            {
                v.mVisible = false;
                d.mCardState++;
            }
            break;
        default:
            self.mExists = false;
            break;
        }
        return;
    }
    if (self.mV->mFlickering)
    {
        if (self.mT->mY > d.mEndY)
        {
            MoveTo(self.mT->mY, d.mEndY, Fx(30));
        }
    }
    else
    {
        self.mExists = false;
    }
}

const EntityClass kTreasure = {
    "TreasureItem", TreasureUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void ShowTreasureDialogue(int id)
{
    if (id == -2)
    {
        Game_ShowText("This person broke everything.");
    }
    else if (id != -1)
    {
        Game_ShowDialogueFor("misc", "any", "treasure", id);
    }
}

void GiveBroom(BroomType type)
{
    gGame.mHasBroom[type] = true;
    if (type == BROOM_NORMAL)
    {
        Inventory_SetEquippedBroom(BROOM_NORMAL);
    }
    if (Inventory_EquippedBroom() == BROOM_NONE)
    {
        Inventory_SetEquippedBroom(type);
    }
}

Entity* MakeTreasureItem(uint8_t kind, const char* sheet, int frame, fx x, fx y)
{
    Entity* item = Ecs_Create(kTreasure, x, y, PART_VISUAL | PART_BRAIN);
    if (item)
    {
        item->mB->mW = item->mB->mH = 16;
        Ecs_SetSolid(*item, false);
        Ecs_SetSheet(*item, sheet);
        Ecs_SetFrame(*item, frame);
        item->mV->mLayer = LAYER_FG_SPRITES;
        TreasureData& d = item->Data<TreasureData>();
        d.mKind = kind;
        d.mEndY = y - Fx(16);
        if (kind == TK_CARD)
        {
            item->mV->mScale = kFxOne / 2;
            item->mV->mOffX = item->mV->mOffY = Fx(12);
            item->mT->mX += Fx(9);
        }
        else
        {
            Ecs_Flicker(*item, FxSec(1.0f));
        }
    }
    return item;
}

// CardTreasure.GetTreasure
void GiveCard(Entity* item, int card)
{
    if (gGame.mCardStatus[card])
    {
        if (item)
        {
            item->mExists = false;
        }
        Ecs_FireEvent(GEV_EMPTY_TREASURE);
    }
    else
    {
        gGame.mCardStatus[card] = 1;
    }
}

// TreasureChest.SetTreasure + BaseTreasure.GetTreasure: the chest's contents appear and take effect.
void OpenTreasure(Entity& chest)
{
    const MapEntity& p = *chest.mPreset;
    const int f = p.mFrame;
    uint8_t kind = TK_FAILSAFE;
    if (f == 0)
    {
        kind = TK_BROOM;
    }
    else if (f == 1)
    {
        kind = TK_KEY;
    }
    else if (f == 2)
    {
        kind = p.mLinkId >= 0 ? TK_CARD : TK_FAILSAFE;
    }
    else if (f == 4)
    {
        kind = TK_WIDE;
    }
    else if (f == 5)
    {
        kind = TK_LONG;
    }
    else if (f == 6)
    {
        kind = TK_SWAP;
    }
    else if (f >= 7 && f <= 20)
    {
        kind = TK_SECRET;
    }

    const char* sheet = "PixieCatSupreme";
    int frame = 0;
    int dialogue = -1;
    switch (kind)
    {
    case TK_BROOM:
        sheet = "broom-icon";
        dialogue = 1;
        break;
    case TK_WIDE:
        sheet = "item_wide_attack";
        dialogue = 4;
        break;
    case TK_LONG:
        sheet = "item_long_attack";
        dialogue = 5;
        break;
    case TK_SWAP:
        sheet = "item_tranformer";
        dialogue = 6;
        break;
    case TK_KEY:
        sheet = "key";
        dialogue = Event_Get("ReceivedKey") == 0 ? 2 : -1;
        break;
    case TK_CARD:
        sheet = p.mLinkId != 48 ? "card_sheet" : "card_49";
        frame = p.mLinkId;
        break;
    case TK_SECRET:
        sheet = "secret_trophies";
        frame = f - 7;
        dialogue = f == 10 ? 7 : -1;
        break;
    default:
        dialogue = -2;
        break;
    }

    Game_PlaySfx("gettreasure");
    ShowTreasureDialogue(dialogue);

    Entity* item = MakeTreasureItem(kind, sheet, frame, chest.mT->mX, chest.mT->mY);

    switch (kind)
    {
    case TK_BROOM:
        GiveBroom(BROOM_NORMAL);
        break;
    case TK_WIDE:
        GiveBroom(BROOM_WIDE);
        break;
    case TK_LONG:
        GiveBroom(BROOM_LONG);
        break;
    case TK_SWAP:
        GiveBroom(BROOM_TRANSFORMER);
        break;
    case TK_KEY:
        Event_Inc("ReceivedKey");
        Inventory_AddMapKey();
        break;
    case TK_CARD:
        GiveCard(item, p.mLinkId);
        break;
    case TK_SECRET:
        gGame.mSecretStatus[f - 7] = true;
        break;
    default:
        break;
    }
}

struct ChestData final
{
    bool mOpened;
};

void ChestCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

bool ChestInteract(Entity& self, Facing playerFacing)
{
    ChestData& d = self.Data<ChestData>();
    if (d.mOpened || playerFacing != FACE_UP)
    {
        return false;
    }
    d.mOpened = true;
    Minimap_AddInterest(gGame.mGridX, gGame.mGridY, -1);
    OpenTreasure(self);
    Ecs_SetFrame(self, Ecs_Frame(self) + 1);
    Preset_SetActivated(*self.mPreset, true);
    return true;
}

const EntityClass kChest = {
    "Treasure", nullptr, nullptr, ChestCollided, ChestInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER, ECF_INTERACTABLE,
};

// WindmillTreasureChest: shut until the windmill is opened
bool WindmillChestInteract(Entity& self, Facing playerFacing)
{
    if (Event_Get("WindmillOpened") == 0)
    {
        Game_ShowDialogueFor("misc", "any", "treasure", 0);
        return true;
    }
    return ChestInteract(self, playerFacing);
}

const EntityClass kWindmillChest = {
    "WindmillTreasureChest", nullptr, nullptr, ChestCollided, WindmillChestInteract, nullptr, nullptr, nullptr,
    0,                       GROUP_PLAYER,    ECF_INTERACTABLE,
};

// ---- Button (Button.cs) ---------------------------------------------------------------------------------------
struct ButtonData final
{
    bool mPressed;
    bool mIncremented;
    bool mPermanent;
};

void ButtonCollided(Entity& self, Entity& other)
{
    ButtonData& d = self.Data<ButtonData>();
    if (&other == World_Player())
    {
        d.mPressed |= Player_State() == PS_GROUND;
    }
    else
    {
        d.mPressed = true;
    }
}

void ButtonUpdate(Entity& self)
{
    ButtonData& d = self.Data<ButtonData>();
    if (d.mPressed && !d.mIncremented)
    {
        d.mIncremented = true;
        gGame.mPuzzlesSolved++;
        Ecs_SetFrame(self, Ecs_Frame(self) + 1);
        Game_PlaySfx("button_down");
    }
    else if (!d.mPressed && d.mIncremented && !d.mPermanent)
    {
        gGame.mPuzzlesSolved--;
        d.mIncremented = false;
        Ecs_SetFrame(self, Ecs_Frame(self) - 1);
        Game_PlaySfx("button_up");
    }
    d.mPressed = false;
    Ecs_BaseUpdate(self);
}

const EntityClass kButton = {
    "Button", ButtonUpdate, nullptr, ButtonCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER | GROUP_SHIELDY | GROUP_MOVER | GROUP_RAT | GROUP_SILVERFISH | GROUP_BLUE_MITRA, 0,
};

// ---- SolidTile / Blocker / VerticalBlocker (Blockers.cs) --------------------------------------------------------
void SolidTileCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kSolidTile = {
    "SolidTile", nullptr, nullptr, SolidTileCollided, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_PART_OF_MAP,
};

void BlockerUpdate(Entity& self)
{
    Entity& p = *World_Player();
    if (Ecs_Intersects(Ecs_Hitbox(p), Ecs_Hitbox(self)))
    {
        if (p.mT->mVelY > 0 && p.mT->mY < self.mT->mY)
        {
            p.mT->mY = self.mT->mY - Fx(p.mB->mH);
        }
        else if (p.mT->mVelY < 0 && p.mT->mY > self.mT->mY)
        {
            p.mT->mY = self.mT->mY + Fx(self.mB->mH);
        }
    }
}

const EntityClass kBlocker = {
    "Blocker", BlockerUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void VBlockerUpdate(Entity& self)
{
    Entity& p = *World_Player();
    if (Ecs_Intersects(Ecs_Hitbox(p), Ecs_Hitbox(self)))
    {
        if (p.mT->mVelX < 0)
        {
            p.mT->mX = self.mT->mX + Fx(self.mB->mW);
        }
        else
        {
            p.mT->mX = self.mT->mX - Fx(p.mB->mW);
        }
    }
}

const EntityClass kVBlocker = {
    "VBlocker", VBlockerUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- Dust (Dust.cs) ---------------------------------------------------------------------------------------------
struct DustData final
{
    bool mOnConveyor; // ON_CONVEYOR
    bool mIsRaft;     // IS_RAFT
};

void DustCollided(Entity& self, Entity& o)
{
    if (!Ecs_AnimFinished(self) && Ecs_AnimIs(self, "unpoof") && Player_BroomDust() == &self)
    {
        self.mExists = false;
        Player_DustMerged();
    }
}

void DustPostUpdate(Entity& self)
{
    self.mT->mVelX = self.mT->mVelY = 0;
    self.Data<DustData>().mOnConveyor = false;
    if (Ecs_AnimFinished(self) && (Ecs_AnimIs(self, "fallpoof") || Ecs_AnimIs(self, "poof")))
    {
        self.mExists = false;
        if (Ecs_AnimIs(self, "fallpoof"))
        {
            Ecs_FireEvent(GEV_DUST_FALL);
        }
    }
}

void DustMapEvent(Entity& self, MapEvent ev, uint8_t dir, fx, fx)
{
    DustData& d = self.Data<DustData>();
    // Dust.Fall: only dust just put down over a hole falls in.
    if (ev == MAPEV_FALL && !d.mIsRaft && Ecs_AnimIs(self, "unpoof") && Ecs_AnimFinished(self))
    {
        Ecs_Play(self, "fallpoof");
    }
    else if (ev == MAPEV_CONVEYOR)
    {
        if (dir != TOUCH_ANY && !d.mIsRaft)
        {
            self.mT->mVelX = Fx(10 * (dir == TOUCH_RIGHT ? 1 : (dir == TOUCH_LEFT ? -1 : 0)));
            self.mT->mVelY = Fx(10 * (dir == TOUCH_DOWN ? 1 : (dir == TOUCH_UP ? -1 : 0)));
        }
        d.mOnConveyor = true;
    }
}

void DustDestroy(Entity& self)
{
    Player_DustFreed(self);
}

const EntityClass kDust = {
    "Dust",     nullptr,    DustPostUpdate, DustCollided, nullptr, DustMapEvent, nullptr, DustDestroy,
    GROUP_DUST, GROUP_DUST, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_PROBE_OWNED,
};

Entity* MakeDust(fx x, fx y, const MapEntity* preset)
{
    Entity* e = Ecs_Create(kDust, x, y, PART_VISUAL | PART_BRAIN, preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 16;
    Ecs_SetSheet(*e, "dust");
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_Play(*e, "poofed");
    return e;
}

// ---- SwitchPillar / PillarSwitch (Switches/*.cs) ------------------------------------------------------------
// Hitting a PillarSwitch flips GlobalState.PillarSwitchOn; each SwitchPillar is up (solid) or down by it.
struct PillarData final
{
    int mDefaultFrame;
    int mCurrent;
    fx mHitTimer; // PillarSwitch.hit_tm
};

int PillarTarget(const PillarData& d)
{
    const int t = d.mDefaultFrame - gGame.mPillarSwitchOn;
    return t < 0 ? -t : t;
}

void SwitchPillarUpdate(Entity& self)
{
    PillarData& d = self.Data<PillarData>();
    Ecs_BaseUpdate(self);
    if (PillarTarget(d) != d.mCurrent)
    {
        if (d.mCurrent == 1)
        {
            Game_PlaySfx("dash_pad_2");
            Ecs_Play(self, "solidify");
        }
        else
        {
            Game_PlaySfx("dash_pad_1");
            Ecs_Play(self, "dissolve");
        }
        d.mCurrent = PillarTarget(d);
    }
}

void SwitchPillarCollided(Entity& self, Entity& other)
{
    if (Ecs_Frame(self) == 0)
    {
        Ecs_Separate(self, other);
    }
}

const EntityClass kSwitchPillar = {
    "SwitchPillar", SwitchPillarUpdate, nullptr, SwitchPillarCollided, nullptr, nullptr, nullptr, nullptr,
    0,              0,                  ECF_PART_OF_MAP,
};

void PillarSwitchUpdate(Entity& self)
{
    PillarData& d = self.Data<PillarData>();
    Ecs_BaseUpdate(self);
    if (d.mHitTimer > 0)
    {
        d.mHitTimer -= kFxTick;
        if (d.mHitTimer <= 0)
        {
            Ecs_Play(self, gGame.mPillarSwitchOn == 0 ? "up" : "down");
        }
    }
}

void PillarSwitchCollided(Entity& self, Entity& other)
{
    PillarData& d = self.Data<PillarData>();
    Ecs_Separate(self, other);
    if ((other.mClass->mGroups & GROUP_BROOM) && d.mHitTimer <= 0)
    {
        Ecs_Play(self, "hit");
        d.mHitTimer = FxF(1.0f);
        Game_PlaySfx("broom_hit");
        gGame.mPillarSwitchOn = 1 - gGame.mPillarSwitchOn;
    }
}

const EntityClass kPillarSwitch = {
    "PillarSwitch", PillarSwitchUpdate, nullptr, PillarSwitchCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_BROOM,        ECF_PART_OF_MAP,
};

// ---- CrackedTile / Hole (Holes/*.cs) ----------------------------------------------------------------------------
void HoleCollided(Entity& self, Entity&)
{
    if (self.mExists && Player_State() != PS_AIR)
    {
        Player_Fall(self.mT->mX, self.mT->mY);
    }
}

const EntityClass kHole = {
    "Hole", nullptr, nullptr, HoleCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

struct CrackedData final
{
    fx mTimer;
};

void CrackedCollided(Entity& self, Entity&)
{
    if (!self.mExists || Player_State() == PS_AIR)
    {
        return;
    }
    CrackedData& d = self.Data<CrackedData>();
    d.mTimer -= kFxTick;
    if (d.mTimer < 0)
    {
        Game_PlaySfx("floor_crack");
        self.mExists = false;
        Entity* hole = Ecs_Create(kHole, self.mT->mX, self.mT->mY, PART_VISUAL);
        if (hole)
        {
            hole->mB->mW = hole->mB->mH = 16;
            hole->mB->mImmovable = true;
            Ecs_SetSolid(*hole, false);
            Ecs_SetSheet(*hole, "hole");
            Ecs_SetFrame(*hole, self.mPreset->mFrame);
            hole->mV->mLayer = LAYER_MAP_BG2;
        }
    }
}

const EntityClass kCrackedTile = {
    "CrackedTile", nullptr, nullptr, CrackedCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};


// ---- KeyCardGate + KeyBlockSentinel (Gates/*.cs) -----------------------------------------------------------------
enum GateKind : uint8_t
{
    GK_SMALL_KEY,
    GK_CARD,
    GK_BIG_KEY,
};

struct KeyGateData final
{
    uint8_t mKind;
    uint8_t mKeyhole; // BigKeyGate: the keyhole frame (green 0, red 1, blue 2)
    int8_t mStep;     // BigGate.OpeningSequence: the frame shown (-1 before the lock breaks)
    fx mTimer;
    Entity* mParts[3]; // keyhole and digits
    Entity* mKey;      // BigKeyGate.keyAnim: the key flying into the lock
    uint8_t mKeyStep;
};

struct SentinelData final
{
    fx mActivationTime;
    fx mMaxActivationTime;
    bool mPlayerCollided;
    bool mTriedOpening;
    bool mOpensOnInteract;
};

void KeyGateCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

void KeyGateDestroy(Entity& self)
{
    KeyGateData& d = self.Data<KeyGateData>();
    for (Entity* part : d.mParts)
    {
        if (part)
        {
            Ecs_Free(*part);
        }
    }
    if (d.mKey)
    {
        Ecs_Free(*d.mKey);
    }
}

// BigCardGate.OnEvent: the sage's ChangeCardCount(92) blows the lock and shows the new count.
void KeyGateOnEvent(Entity& self, int gameEvent)
{
    KeyGateData& d = self.Data<KeyGateData>();
    if (gameEvent != GEV_CHANGE_CARD_COUNT || d.mKind != GK_CARD)
    {
        return;
    }
    Entity* boom = Spawn_Explosion(*d.mParts[0]);
    if (boom)
    {
        boom->mT->mX = d.mParts[0]->mT->mX + Fx(5);
        boom->mT->mY = d.mParts[0]->mT->mY - Fx(4);
        boom->mT->mLastX = boom->mT->mX;
        boom->mT->mLastY = boom->mT->mY;
        boom->mV->mLayerRef = &self;
        boom->mV->mLayerOffset = 2;
    }
    constexpr int kCount = 92;
    Ecs_SetFrame(*d.mParts[1], kCount / 10);
    Ecs_SetFrame(*d.mParts[2], kCount % 10);
}

const EntityClass kKeyGate = {
    "KeyGate", nullptr, nullptr, KeyGateCollided, nullptr, nullptr, KeyGateOnEvent, KeyGateDestroy, 0, 0,
    ECF_PART_OF_MAP,
};

void BreakLock(Entity& gate)
{
    KeyGateData& d = gate.Data<KeyGateData>();
    for (Entity* part : d.mParts)
    {
        if (part)
        {
            part->mExists = false;
        }
    }
}

// BigGate.OpeningSequence: five shakes 0.8 s apart, then the gate goes. (Each wait starts in the frame the last one
// ended, as the C# loop runs straight on into the next MoveTo.)
bool GateOpeningStep(Entity& gate)
{
    KeyGateData& d = gate.Data<KeyGateData>();
    for (;;)
    {
        if (!MoveTo(d.mTimer, FxF(0.8f), kFxOne))
        {
            return false;
        }
        d.mTimer = 0;
        if (d.mStep >= 4)
        {
            break;
        }
        d.mStep++;
        Screen_Shake(FxF(0.02f), FxF(0.3f));
        Game_PlaySfx("hit_ground_1");
        if (d.mStep == 0)
        {
            BreakLock(gate);
        }
        Ecs_SetFrame(gate, d.mStep);
    }
    Game_PlaySfx("open");
    gate.mExists = false;
    return true;
}

const EntityClass kGateKey = {
    "BigKeyGateKey", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// BigKeyGate.keyAnim: the key fades in over the player, flies to the lock and fades out, then OpeningSequence. (Its
// sprite is the cutscene's own entity in the C#, not the play state's.)
bool GateKeyStep(Entity& gate)
{
    KeyGateData& d = gate.Data<KeyGateData>();
    switch (d.mKeyStep)
    {
    case 0:
    {
        const Entity& p = *World_Player();
        d.mKey = Ecs_Create(kGateKey, p.mT->mX, p.mT->mY - Fx(16), PART_VISUAL, nullptr, &gate);
        if (!d.mKey)
        {
            return true;
        }
        Ecs_SetSolid(*d.mKey, false);
        Ecs_SetSheet(*d.mKey, "key_green");
        Ecs_SetFrame(*d.mKey, d.mKeyhole * 2);
        d.mKey->mV->mLayer = LAYER_FG_SPRITES;
        d.mKey->mV->mOpacity = 0;
        d.mKeyStep = 1;
        return false;
    }
    case 1:
        if (!MoveTo(d.mKey->mV->mOpacity, kFxOne, FxF(0.6f)))
        {
            return false;
        }
        Ecs_Flicker(*d.mKey, FxSec(0.5f));
        d.mKeyStep = 2;
        [[fallthrough]];
    case 2:
    {
        Transform& t = *d.mKey->mT;
        const bool doneX = MoveTo(t.mX, gate.mT->mX + Fx(8), Fx(6));
        if (!(MoveTo(t.mY, gate.mT->mY, Fx(6)) && doneX))
        {
            return false;
        }
        Game_PlaySfx("player_jump_down");
        d.mKeyStep = 3;
        [[fallthrough]];
    }
    default:
        if (!MoveTo(d.mKey->mV->mOpacity, 0, FxF(0.7f)))
        {
            return false;
        }
        Ecs_Free(*d.mKey);
        d.mKey = nullptr;
        d.mStep = -1;
        d.mTimer = 0;
        Game_StartCutscene(gate, GateOpeningStep);
        return true;
    }
}

bool TryUnlock(Entity& gate)
{
    KeyGateData& d = gate.Data<KeyGateData>();
    const int f = gate.mPreset->mFrame;
    switch (d.mKind)
    {
    case GK_SMALL_KEY:
        if (Inventory_MapKeys() > 0)
        {
            Inventory_RemoveMapKey();
            Game_PlaySfx("unlock");
            Ecs_Play(gate, "Open");
            Ecs_SetSolid(gate, false);
            return true;
        }
        Game_ShowDialogueFor("misc", "any", "keyblock", 0);
        return false;
    case GK_CARD:
        if (Inventory_CardCount() >= f)
        {
            if (f == 4)
            {
                Game_ShowDialogueFor("misc", "any", "keyblockgate", 1);
            }
            else if (f < 36)
            {
                Game_ShowDialogueFor("misc", "any", "keyblockgate", 4);
            }
            else if (f == 36)
            {
                Game_ShowDialogueFor("misc", "any", "keyblockgate", 3);
            }
            else if (f == 47)
            {
                Game_ShowDialogueFor("misc", "any", "keyblockgate", 5);
            }
            else if (f == 49)
            {
                Game_ShowText("!!!");
            }
            d.mStep = -1;
            d.mTimer = 0;
            Game_StartCutscene(gate, GateOpeningStep);
            return true;
        }
        if (f == 4)
        {
            Game_ShowDialogueFor("misc", "any", "keyblockgate", 0);
        }
        else if (f == 49)
        {
            Game_ShowText("....");
        }
        else if (f == 50)
        {
            Game_ShowDialogueFor("misc", "any", "keyblockgate", 6);
        }
        else
        {
            Game_ShowDialogueFor("misc", "any", "keyblockgate", 2);
        }
        return false;
    default:
        if (gGame.mBigKeyStatus[d.mKeyhole])
        {
            d.mKeyStep = 0;
            Game_StartCutscene(gate, GateKeyStep);
            return true;
        }
        return false;
    }
}

void SentinelUpdate(Entity& self)
{
    SentinelData& d = self.Data<SentinelData>();
    if (!d.mPlayerCollided)
    {
        d.mActivationTime = d.mMaxActivationTime;
    }
    d.mPlayerCollided = false;
}

void SentinelOpen(Entity& self)
{
    Entity* gate = self.mParent;
    if (gate && TryUnlock(*gate))
    {
        Preset_SetAlive(*gate->mPreset, false);
        self.mExists = false;
    }
}

void SentinelCollided(Entity& self, Entity&)
{
    SentinelData& d = self.Data<SentinelData>();
    if (d.mOpensOnInteract || d.mTriedOpening || Player_State() != PS_GROUND)
    {
        return;
    }
    d.mActivationTime -= kFxTick;
    d.mPlayerCollided = true;
    if (d.mActivationTime < 0)
    {
        SentinelOpen(self);
        d.mTriedOpening = true;
    }
}

bool SentinelInteract(Entity& self, Facing)
{
    if (!self.Data<SentinelData>().mOpensOnInteract)
    {
        return false;
    }
    SentinelOpen(self);
    return true;
}

const EntityClass kSentinel = {
    "KeyBlockSentinel", SentinelUpdate, nullptr, SentinelCollided, SentinelInteract, nullptr, nullptr, nullptr,
    0,                  GROUP_PLAYER,   ECF_INTERACTABLE,
};

const EntityClass kGatePart = {
    "GatePart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* MakeGatePart(Entity& gate, fx dx, fx dy, const char* sheet, int frame, int layerOffset)
{
    Entity* e = Ecs_Create(kGatePart, gate.mT->mX + dx, gate.mT->mY + dy, PART_VISUAL, nullptr, &gate);
    if (e)
    {
        Ecs_SetSolid(*e, false);
        Ecs_SetSheet(*e, sheet);
        Ecs_SetFrame(*e, frame);
        e->mV->mLayerRef = &gate;
        e->mV->mLayerOffset = static_cast<int8_t>(layerOffset);
        e->mPreset = gate.mPreset; // kept (not freed as dead) while the gate lives
    }
    return e;
}

Entity* SpawnKeyGate(const MapEntity& preset, uint8_t kind)
{
    const bool big = kind != GK_SMALL_KEY;
    Entity* e = Spawn_Basic(kKeyGate, preset, big ? "gate_green" : "keyhole", big ? 32 : 16, 16);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    if (big)
    {
        e->mB->mW = 32;
    }
    KeyGateData& d = e->Data<KeyGateData>();
    d.mKind = kind;
    Entity* s = Ecs_Create(kSentinel, e->mT->mX - Fx(2), e->mT->mY - Fx(2), PART_BRAIN, nullptr, e);
    if (s)
    {
        s->mB->mW = s->mB->mH = 20;
        SentinelData& sd = s->Data<SentinelData>();
        sd.mMaxActivationTime = FxSec(0.2f);
        if (big)
        {
            s->mT->mX = e->mT->mX + Fx(11);
            s->mT->mY = e->mT->mY - Fx(3);
            s->mB->mW = 10;
        }
        if (kind == GK_CARD)
        {
            sd.mOpensOnInteract = true;
        }
        if (kind == GK_BIG_KEY)
        {
            sd.mMaxActivationTime = 0;
        }
        sd.mActivationTime = sd.mMaxActivationTime;
    }
    if (kind == GK_CARD)
    {
        Ecs_SetFrame(*e, 0);
        d.mParts[0] = MakeGatePart(*e, 0, 0, "gate_green_slots", 3, 1);
        d.mParts[1] = MakeGatePart(*e, Fx(12), Fx(6), "gate_green_digits", preset.mFrame / 10, 2);
        d.mParts[2] = MakeGatePart(*e, Fx(17), Fx(6), "gate_green_digits", preset.mFrame % 10, 2);
    }
    else if (kind == GK_BIG_KEY)
    {
        Ecs_SetFrame(*e, 0);
        d.mKeyhole = preset.mFrame == 1 ? 2 : (preset.mFrame == 2 ? 0 : 1);
        d.mParts[0] = MakeGatePart(*e, 0, 0, "gate_green_slots", d.mKeyhole, 1);
    }
    else
    {
        Ecs_Play(*e, "closed");
    }
    return e;
}

// ---- Key (Key.cs) -----------------------------------------------------------------------------------------------
struct KeyData final
{
    bool mBossRush;
};

void KeyUpdate(Entity& self)
{
    if (!self.mV->mVisible && self.Data<KeyData>().mBossRush && gGame.mEnemiesKilled > 0)
    {
        self.mV->mVisible = true;
    }
    Ecs_BaseUpdate(self);
}

void KeyCollided(Entity& self, Entity&)
{
    if (self.mV->mVisible)
    {
        self.mV->mVisible = false;
        Inventory_AddMapKey();
        Game_PlaySfx("keyget");
        Preset_SetAlive(*self.mPreset, false);
        self.mExists = false;
    }
}

const EntityClass kKey = {
    "Key", KeyUpdate, nullptr, KeyCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER | GROUP_BROOM, 0,
};

// ---- Health pickups (HealthPickup.cs, HealthEntity.cs) -----------------------------------------------------------
struct HealthData final
{
    int mHeal;
    fx mLatency;
};

void HealthUpdate(Entity& self)
{
    self.Data<HealthData>().mLatency -= kFxTick;
    Ecs_BaseUpdate(self);
}

void HealthCollided(Entity& self, Entity&)
{
    HealthData& d = self.Data<HealthData>();
    if (d.mLatency <= 0)
    {
        Game_PlaySfx("get_small_health");
        GameState_SetHealth(gGame.mCurHealth + d.mHeal);
        self.mExists = false;
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
    }
}

const EntityClass kHealth = {
    "HealthPickup", HealthUpdate, nullptr, HealthCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER | GROUP_BROOM, 0,
};

Entity* MakeHealth(fx x, fx y, bool big, const MapEntity* preset)
{
    Entity* e = Ecs_Create(kHealth, x, y, PART_VISUAL | PART_BRAIN, preset);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetSheet(*e, big ? "big_health_pickup" : "small_health_pickup");
    Ecs_SetFrameBase(*e, Game_IsCell() ? 4 : 0);
    e->mB->mW = static_cast<int16_t>(big ? 16 : 10);
    e->mB->mH = 16;
    Ecs_Play(*e, "float");
    HealthData& d = e->Data<HealthData>();
    d.mHeal = big ? 3 : 1;
    d.mLatency = FxSec(0.5f);
    return e;
}

// ---- Explosion (Explosion.cs) -------------------------------------------------------------------------------------
// (it stays on its last frame until the screen goes, as the C#'s does)
void ExplosionUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
}

const EntityClass kExplosion = {
    "Explosion", ExplosionUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- Doors (Door.cs, Doors/*.cs) -----------------------------------------------------------------------------------
// Each Door preset is an entity like the C#'s; where it leads (the linked door + teleportOffset) is the content
// compiler's MapDoor.
enum DoorType : uint8_t
{
    DOOR_BLANK_PORTAL = 4,
    DOOR_WHIRLPOOL = 7,
    DOOR_FALL = 8,
    DOOR_NEXUS_PAD = 16,
    DOOR_NEXUS = 17,
};

struct DoorData final
{
    const MapDoor* mDoor;
    bool mPlayerOnDoor; // player_on_door
    bool mPadOn;        // NexusPad.playerCollission
    bool mActivated;    // NexusDoor.activated
    bool mInactive;     // !Door.Active (a DoorToggle shut it)
    bool mNoSmack;      // !FallDoor.smack_on_next_screen
};

bool PlayerOn(const Entity& self)
{
    return Ecs_Intersects(Ecs_Hitbox(*Player_Entity()), Ecs_Hitbox(self));
}

// Door.TeleportPlayer
void DoorTeleport(Entity& self)
{
    DoorData& d = self.Data<DoorData>();
    World_TakeDoor(*d.mDoor);
    d.mPlayerOnDoor = true;
    const uint8_t type = d.mDoor->mType;
    if (type == DOOR_BLANK_PORTAL || type == DOOR_NEXUS)
    {
        Game_PlaySfx("teleport_up");
    }
    else if (type != DOOR_WHIRLPOOL && type != DOOR_NEXUS_PAD)
    {
        Game_PlaySfx("enter_door");
    }
}

void DoorUpdate(Entity& self)
{
    DoorData& d = self.Data<DoorData>();
    Ecs_BaseUpdate(self);
    if (!PlayerOn(self))
    {
        d.mPlayerOnDoor = false;
    }
    switch (d.mDoor->mType)
    {
    case DOOR_WHIRLPOOL:
        if (Ecs_AnimFinished(self))
        {
            Ecs_Play(self, "whirl_red");
        }
        break;
    case DOOR_NEXUS_PAD:
        if (d.mPadOn && !PlayerOn(self))
        {
            d.mPadOn = false;
            Ecs_Play(self, "off");
        }
        break;
    case DOOR_NEXUS:
        if (d.mActivated && Player_JustLanded())
        {
            DoorTeleport(self);
        }
        break;
    default:
        break;
    }
}

void DoorCollided(Entity& self, Entity& other)
{
    DoorData& d = self.Data<DoorData>();
    if (&other != Player_Entity())
    {
        return;
    }
    switch (d.mDoor->mType)
    {
    case DOOR_NEXUS_PAD:
        if (!d.mPadOn && Player_State() == PS_GROUND)
        {
            Ecs_Play(self, "on");
            Game_PlaySfx("menu_select");
            d.mPadOn = true;
        }
        break;
    case DOOR_NEXUS:
        Ecs_Separate(self, other);
        break;
    case DOOR_FALL:
        if (d.mInactive)
        {
            break;
        }
        if (Player_State() == PS_GROUND || (Player_State() == PS_ENTER_FALL && Player_OffsetY() == 0))
        {
            DoorTeleport(self);
            Player_EnterFall(!d.mNoSmack);
            d.mInactive = true;
        }
        else if (Player_State() == PS_AUTO_JUMP)
        {
            // The first jump lands on the top floor's door: only the second door's fall ends in a smack
            d.mNoSmack = true;
        }
        break;
    default:
        if (!d.mPlayerOnDoor && !d.mInactive)
        {
            DoorTeleport(self);
        }
        break;
    }
}

bool DoorInteract(Entity& self, Facing playerFacing)
{
    DoorData& d = self.Data<DoorData>();
    if (d.mDoor->mType == DOOR_NEXUS_PAD)
    {
        DoorTeleport(self);
        return true;
    }
    // NexusDoor: jump into the portal of a map whose pad has been stood on
    const bool previewExists = (gGame.mNexusPortals >> d.mDoor->mDestMap) & 1;
    if (previewExists && playerFacing == FACE_UP && !d.mActivated)
    {
        d.mActivated = true;
        const Entity& p = *Player_Entity();
        Player_AutoJump(FxF(0.4f), p.mT->mX, p.mT->mY - Fx(16));
        return true;
    }
    return false;
}

const EntityClass kDoor = {
    "Door", DoorUpdate, nullptr, DoorCollided, nullptr, nullptr, nullptr, nullptr, GROUP_DOOR, GROUP_PLAYER, 0,
};
// DoorToggle: flips Active on every door it overlaps, then goes (its Update runs after the collisions).
struct DoorToggleData final
{
    bool mHitDoor;
};

void DoorToggleUpdate(Entity& self)
{
    if (self.Data<DoorToggleData>().mHitDoor)
    {
        self.mExists = false;
    }
}

void DoorToggleCollided(Entity& self, Entity& other)
{
    DoorData& d = other.Data<DoorData>();
    d.mInactive = !d.mInactive;
    self.Data<DoorToggleData>().mHitDoor = true;
}

const EntityClass kDoorToggle = {
    "DoorToggle", DoorToggleUpdate, nullptr, DoorToggleCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_DOOR, 0,
};

} // namespace

// GlobalState.SpawnEntity(new DoorToggle(position, w, h)).
void Spawn_DoorToggle(fx x, fx y, int w, int h)
{
    Entity* t = Ecs_Create(kDoorToggle, x, y, PART_BRAIN);
    if (t)
    {
        Ecs_MarkSpawned(*t);
        t->mB->mW = w;
        t->mB->mH = h;
        t->Data<DoorToggleData>() = DoorToggleData();
    }
}

namespace
{

// RedCaveEntrance (Solid_Sprite red_cave_*_ss): the cave mouth, there once its map event reached the count; until then
// it isn't, and its door is shut.
void RedCaveEntranceCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kRedCaveEntrance = {
    "RedCaveEntrance", nullptr, nullptr, RedCaveEntranceCollided, nullptr, nullptr, nullptr, nullptr, 0, 0,
    ECF_PART_OF_MAP,
};

// Red_Pillar: its chain (a sub-entity) takes a broom hit and starts the cutscene where the pillar sinks; the cave
// mouth it counts towards rises on the Red Sea (shown there) once its last pillar is down.
enum PillarStep : uint8_t
{
    PILLAR_START,
    PILLAR_SINK,
    PILLAR_SHOW_DOOR,
    PILLAR_WARP,
    PILLAR_RISE_START,
    PILLAR_RISE,
    PILLAR_WAIT,
};

struct RedPillarData final
{
    uint8_t mStep;
    fx mTime;
    Entity* mChain;
    Entity* mRipple;
    Entity* mDoor; // the cutscene's own
    char mWarp;    // 'n', 'l' or 'r'
};

void RedPillarCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kRedPillar = {
    "Red_Pillar", nullptr, nullptr, RedPillarCollided, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_PART_OF_MAP,
};

// A plain Entity (the C# makes these with no class of their own): just its sprite.
const EntityClass kPlainEntity = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void RedPillarEnd(Entity& self)
{
    RedPillarData& d = self.Data<RedPillarData>();
    Preset_SetAlive(*self.mPreset, false);
    self.mExists = false;
    if (d.mRipple)
    {
        d.mRipple->mExists = false;
        d.mRipple = nullptr;
    }
    if (d.mDoor)
    {
        Ecs_Free(*d.mDoor);
        d.mDoor = nullptr;
    }
    World_CutsceneReturn();
}

// One frame of the coroutine: false at each yield, true once it is done.
bool RedPillarCutscene(Entity& self)
{
    RedPillarData& d = self.Data<RedPillarData>();
    const fx height = Fx(self.mB->mH);
    switch (d.mStep)
    {
    case PILLAR_START:
        Ecs_Play(self, "move");
        d.mRipple = Ecs_Create(kPlainEntity, self.mT->mX, self.mT->mY + height - Fx(15), PART_VISUAL);
        if (d.mRipple)
        {
            Ecs_MarkSpawned(*d.mRipple);
            Ecs_SetSheet(*d.mRipple, "red_pillar_ripple");
            Ecs_Play(*d.mRipple, "move");
        }
        Ecs_Flicker(self, FxSec(1.5f));
        Game_PlaySfx("broom_hit");
        d.mStep = PILLAR_SINK;
        [[fallthrough]];
    case PILLAR_SINK:
    {
        if (!MoveTo(self.mV->mYPush, height, FxDiv(height, FxF(1.5f))) || self.mV->mFlickering)
        {
            return false;
        }
        int required = 1;
        char c = 'n';
        switch (self.mPreset->mFrame)
        {
        case 1:
        case 2:
            required = 2;
            break;
        case 3:
            c = 'l';
            break;
        case 4:
            c = 'r';
            break;
        default:
            break;
        }
        char event[16];
        snprintf(event, sizeof(event), "red_cave_%c_ss", c);
        Event_Inc(event);
        if (Event_Get(event) != required)
        {
            RedPillarEnd(self);
            return true;
        }
        d.mWarp = c;
        const int gx = c == 'n' ? 3 : (c == 'l' ? 2 : 4);
        const int gy = c == 'n' ? 3 : 4;
        d.mDoor = Ecs_Create(kPlainEntity, Fx(gx * 160 + 48), Fx(gy * 160 + 48), PART_VISUAL);
        if (d.mDoor)
        {
            d.mDoor->mCutsceneOwned = true;
            Ecs_MarkSpawned(*d.mDoor);
            Ecs_SetSheet(*d.mDoor, "red_cave_left", 64, 64);
            Ecs_SetFrame(*d.mDoor, 0);
            d.mDoor->mB->mW = d.mDoor->mB->mH = 64;
            d.mDoor->mV->mYPush = Fx(64);
        }
        d.mStep = PILLAR_SHOW_DOOR; // yield EntityEvent
        return false;
    }
    case PILLAR_SHOW_DOOR:
    {
        // yield WarpEvent("REDSEA", warp)
        const char c = d.mWarp;
        World_CutsceneWarp(Game_MapIndex("REDSEA"), c == 'n' ? 3 : (c == 'l' ? 2 : 4), c == 'n' ? 3 : 4);
        d.mStep = PILLAR_RISE_START;
        return false;
    }
    case PILLAR_RISE_START:
        Game_PlaySfx("red_cave_rise");
        d.mStep = PILLAR_RISE;
        if (d.mDoor && d.mDoor->mV->mYPush > 0)
        {
            d.mTime = FxF(0.02f);
            Screen_Shake(FxF(0.05f), FxF(0.5f));
        }
        else
        {
            d.mTime = 0;
        }
        [[fallthrough]];
    case PILLAR_RISE:
        for (;;)
        {
            if (d.mTime > 0)
            {
                d.mTime -= kFxTick;
                return false;
            }
            if (!d.mDoor || d.mDoor->mV->mYPush <= 0)
            {
                break;
            }
            d.mDoor->mV->mYPush -= kFxOne;
            if (d.mDoor->mV->mYPush <= 0)
            {
                break;
            }
            d.mTime = FxF(0.02f);
            Screen_Shake(FxF(0.05f), FxF(0.5f));
        }
        d.mStep = PILLAR_WAIT;
        d.mTime = FxF(1.5f);
        [[fallthrough]];
    default:
        if (d.mTime > 0)
        {
            d.mTime -= kFxTick;
            return false;
        }
        RedPillarEnd(self);
        return true;
    }
}

void ChainCollided(Entity& self, Entity&)
{
    Entity* pillar = self.mParent;
    if (pillar)
    {
        pillar->Data<RedPillarData>().mStep = PILLAR_START;
        Game_StartCutscene(*pillar, RedPillarCutscene);
    }
    self.mExists = false;
}

const EntityClass kChain = {
    "Chain", nullptr, nullptr, ChainCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_BROOM, 0,
};

// WhirlPool: a Door the Fisherman collides with.
const EntityClass kWhirlPool = {
    "WhirlPool", DoorUpdate, nullptr, DoorCollided, nullptr, nullptr, nullptr, nullptr, GROUP_WHIRLPOOL | GROUP_DOOR, GROUP_PLAYER, 0,
};
const EntityClass kNexusDoor = {
    "NexusDoor", DoorUpdate, nullptr, DoorCollided, DoorInteract, nullptr, nullptr, nullptr, GROUP_DOOR, GROUP_PLAYER,
    ECF_INTERACTABLE,
};

// ---- Checkpoint (Checkpoint.cs) -----------------------------------------------------------------------------------
enum CheckpointState : uint8_t
{
    CPS_SPAWNED_ON,
    CPS_WAIT_TUTORIAL,
    CPS_WAIT,
    CPS_PLAYER_ON,
    CPS_SAVED,
    CPS_WAIT_FOR_RESAVE,
};

struct CheckpointData final
{
    uint8_t mState;
    bool mInteract;
    fx mTimer;
};

bool CheckpointActive(const Entity& self)
{
    return gGame.mCheckpointMap == gGame.mCurrentMap && gGame.mCheckpointX == FxTrunc(self.mT->mX) &&
           gGame.mCheckpointY == FxTrunc(self.mT->mY);
}

bool CheckpointPlayerOn(const Entity& self)
{
    return Ecs_Intersects(Ecs_Hitbox(*World_Player()), Ecs_Hitbox(self));
}

void CheckpointEnter(Entity& self, uint8_t state)
{
    CheckpointData& d = self.Data<CheckpointData>();
    d.mState = state;
    switch (state)
    {
    case CPS_WAIT:
        Ecs_Play(self, CheckpointActive(self) ? "active" : "inactive");
        break;
    case CPS_PLAYER_ON:
        Ecs_Play(self, "stepped_on");
        break;
    case CPS_SAVED:
        Ecs_Play(self, "active");
        break;
    case CPS_WAIT_FOR_RESAVE:
        d.mTimer = FxSec(0.1f);
        break;
    default:
        break;
    }
}

void CheckpointUpdate(Entity& self)
{
    CheckpointData& d = self.Data<CheckpointData>();
    const bool interact = d.mInteract;
    d.mInteract = false;
    const Entity& p = *World_Player();
    switch (d.mState)
    {
    case CPS_SPAWNED_ON:
        if (!CheckpointPlayerOn(self))
        {
            CheckpointEnter(self, CPS_WAIT);
        }
        break;
    case CPS_WAIT_TUTORIAL:
        if (FxLength(p.mT->mX - self.mT->mX, p.mT->mY - self.mT->mY) < Fx(18))
        {
            Game_ShowDialogueFor("misc", "any", "checkpoint", 3);
            Event_Inc("CheckpointTutorial");
            CheckpointEnter(self, CPS_WAIT);
        }
        break;
    case CPS_WAIT:
        if (CheckpointPlayerOn(self))
        {
            CheckpointEnter(self, CPS_PLAYER_ON);
        }
        break;
    case CPS_PLAYER_ON:
        if (interact)
        {
            Game_PlaySfx("button_down");
            if (!CheckpointActive(self))
            {
                GameState_SetHealth(gGame.mMaxHealth);
                gGame.mCheckpointMap = gGame.mCurrentMap;
                gGame.mCheckpointX = FxTrunc(self.mT->mX);
                gGame.mCheckpointY = FxTrunc(self.mT->mY);
            }
            // (GlobalState.SaveGame and the saving icon: no saves yet.)
            CheckpointEnter(self, CPS_SAVED);
        }
        else if (!CheckpointPlayerOn(self))
        {
            CheckpointEnter(self, CPS_WAIT);
        }
        break;
    case CPS_SAVED:
        if (!CheckpointPlayerOn(self))
        {
            CheckpointEnter(self, CPS_WAIT_FOR_RESAVE);
        }
        break;
    default:
        d.mTimer -= kFxTick;
        if (d.mTimer < 0)
        {
            CheckpointEnter(self, CPS_WAIT);
        }
        break;
    }
    Ecs_BaseUpdate(self);
}

bool CheckpointInteract(Entity& self, Facing)
{
    self.Data<CheckpointData>().mInteract = true;
    return false; // don't disable the broom
}

const EntityClass kCheckpoint = {
    "Checkpoint", CheckpointUpdate, nullptr, nullptr, CheckpointInteract, nullptr, nullptr, nullptr,
    0,            0,                ECF_INTERACTABLE,
};

// ---- SpringPad / JumpTrigger (SpringPad.cs, JumpTrigger.cs) ------------------------------------------------------
struct JumpData final
{
    int mDistance;
    fx mTime;
    bool mPlayerCollision;
    bool mActivated;
};

void JumpSetup(JumpData& d, int frame)
{
    switch (frame)
    {
    case 1:
        d.mDistance = 48;
        d.mTime = FxSec(0.5f);
        break;
    case 2:
        d.mDistance = 64;
        d.mTime = FxSec(0.7f);
        break;
    default:
        d.mDistance = 32;
        d.mTime = FxSec(0.3f);
        break;
    }
}

void SpringCollided(Entity& self, Entity& other)
{
    JumpData& d = self.Data<JumpData>();
    d.mPlayerCollision = true;
    if (Player_JustLanded())
    {
        d.mActivated = true;
        Game_PlaySfx("spring_bounce");
        Ecs_Play(self, "wobble");
        Player_SetFacing(FACE_DOWN);
        Player_AutoJump(d.mTime, other.mT->mX, other.mT->mY + Fx(d.mDistance), 10);
    }
    else if (!d.mActivated && Player_State() == PS_GROUND)
    {
        Ecs_Play(self, "pressed");
    }
    else if (Player_State() == PS_AIR)
    {
        Ecs_Play(self, "still");
    }
}

void SpringUpdate(Entity& self)
{
    JumpData& d = self.Data<JumpData>();
    if ((d.mActivated && Ecs_AnimFinished(self)) || !d.mPlayerCollision)
    {
        d.mActivated = false;
        Ecs_Play(self, "still");
    }
    d.mPlayerCollision = false;
    Ecs_BaseUpdate(self);
}

const EntityClass kSpringPad = {
    "SpringPad", SpringUpdate, nullptr, SpringCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

void JumpTriggerCollided(Entity& self, Entity& other)
{
    const PlayerState s = Player_State();
    if (s == PS_LADDER || (s == PS_GROUND && Player_Facing() == FACE_DOWN))
    {
        const JumpData& d = self.Data<JumpData>();
        Player_AutoJump(d.mTime, other.mT->mX, other.mT->mY + Fx(d.mDistance));
    }
}

const EntityClass kJumpTrigger = {
    "JumpTrigger", nullptr, nullptr, JumpTriggerCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- Events: DungeonEntrance(Ref), BossRushKeyReset ---------------------------------------------------------------
void SetReturnTarget(int x, int y)
{
    if (gGame.mReturnMap == gGame.mCurrentMap)
    {
        return;
    }
    gGame.mReturnMap = gGame.mCurrentMap;
    gGame.mReturnX = x - 10;
    gGame.mReturnY = y - 34;
}
} // namespace

bool Dust_OnConveyor(Entity& dust)
{
    return dust.Data<DustData>().mOnConveyor;
}

bool Dust_IsRaft(Entity& dust)
{
    return dust.Data<DustData>().mIsRaft;
}

void Dust_SetRaft(Entity& dust, bool raft)
{
    dust.Data<DustData>().mIsRaft = raft;
    dust.mPlayerOwned = raft; // Player.SubEntities: the raft goes along with the player
}

void Treasure_GiveCard(fx x, fx y, int card)
{
    Entity* item = MakeTreasureItem(TK_CARD, card != 48 ? "card_sheet" : "card_49", card, x, y);
    Game_PlaySfx("gettreasure");
    GiveCard(item, card);
}

Entity* Spawn_Console(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kConsole, preset, "console");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    if (Preset_Activated(preset))
    {
        Ecs_Play(*e, "green");
        gGame.mPuzzlesSolved++;
    }
    else
    {
        Ecs_Play(*e, "active");
    }
    return e;
}

Entity* Spawn_Gate(const MapEntity& preset, Entity& player)
{
    Entity* e = Spawn_Basic(kGate, preset, "gates");
    if (!e)
    {
        return nullptr;
    }
    const int closed = atoi(Preset_TypeValue(preset));
    Ecs_SetFrameBase(*e, closed);
    e->mB->mImmovable = true;
    GateData& d = e->Data<GateData>();
    d.mHeldDown = Ecs_Intersects(Ecs_Hitbox(player), Ecs_Hitbox(*e));
    // The C# builds "close"/"open" at 10 fps for the first gate graphics (frame 0), else 8 / 4.
    if (d.mHeldDown && !GateCondition(preset))
    {
        Ecs_Play(*e, "opened");
        Ecs_SetSolid(*e, false);
    }
    else
    {
        Ecs_Play(*e, "still");
    }
    if (closed == 0)
    {
        e->mV->mFpsOverride = 10;
    }
    return e;
}

Entity* Spawn_SageBlank(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kSageBlank, Fx(preset.mX), Fx(preset.mY), 0, &preset);
    if (e)
    {
        e->mB->mW = 16;
        e->mB->mH = 16;
        Ecs_SetSolid(*e, false);
    }
    return e;
}

Entity* Spawn_Treasure(const MapEntity& preset, Entity&)
{
    const bool windmill = gGame.mMapName && strcmp(gGame.mMapName, "WINDMILL") == 0;
    Entity* e = Spawn_Basic(windmill ? kWindmillChest : kChest, preset, "treasureboxes");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    int frame = Game_IsCell() ? 4 : 0;
    if (Preset_Activated(preset))
    {
        frame++;
        e->Data<ChestData>().mOpened = true;
    }
    else
    {
        // (the screen it's on: gGame's grid is still the last one while this one loads)
        Minimap_AddInterest(preset.mX / 160, preset.mY / 160, 1);
    }
    Ecs_SetFrame(*e, frame);
    return e;
}

Entity* Spawn_Button(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kButton, preset, "buttons");
    if (!e)
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    e->Data<ButtonData>().mPermanent = preset.mFrame == 0;
    const char* map = gGame.mMapName ? gGame.mMapName : "";
    int frame = 2;
    if (strcmp(map, "STREET") == 0)
    {
        frame = 6;
    }
    else if (strcmp(map, "BEDROOM") == 0)
    {
        frame = 0;
    }
    else if (strcmp(map, "REDCAVE") == 0)
    {
        frame = 4;
    }
    else if (strcmp(map, "CELL") == 0)
    {
        frame = 8;
    }
    Ecs_SetFrame(*e, frame);
    return e;
}

Entity* Spawn_SolidTile(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kSolidTile, Fx(preset.mX), Fx(preset.mY), 0, &preset);
    if (e)
    {
        e->mB->mW = e->mB->mH = 16;
        e->mB->mImmovable = true;
    }
    return e;
}

Entity* Spawn_Blocker(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kBlocker, Fx(preset.mX), Fx(preset.mY), 0, &preset);
    if (e)
    {
        e->mB->mW = 64;
        e->mB->mH = 4;
        e->mB->mImmovable = true;
    }
    return e;
}

Entity* Spawn_VBlocker(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kVBlocker, Fx(preset.mX), Fx(preset.mY), 0, &preset);
    if (e)
    {
        e->mB->mW = 4;
        e->mB->mH = 16;
        e->mB->mImmovable = true;
    }
    return e;
}

Entity* Spawn_Dust(const MapEntity& preset, Entity&)
{
    return MakeDust(Fx(preset.mX), Fx(preset.mY), &preset);
}

Entity* Spawn_DustAt(fx x, fx y)
{
    Entity* e = MakeDust(x, y, nullptr);
    if (e)
    {
        e->mKeep = true; // (no preset to keep it: the broom may be carrying it)
    }
    return e;
}

Entity* Spawn_SwitchPillar(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSwitchPillar, preset, "dame-switch-pillar");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    e->mV->mLayer = LAYER_BG_ENTITIES;
    PillarData& d = e->Data<PillarData>();
    d.mDefaultFrame = 1 - preset.mFrame; // inverse of what the level editor suggests
    Ecs_Play(*e, PillarTarget(d) == 0 ? "up" : "down");
    d.mCurrent = PillarTarget(d);
    return e;
}

Entity* Spawn_PillarSwitch(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kPillarSwitch, preset, "pillar_switch");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_Play(*e, gGame.mPillarSwitchOn == 0 ? "up" : "down");
    return e;
}

Entity* Spawn_CrackedTile(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kCrackedTile, preset, "crackedtiles");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, false);
    Ecs_SetFrame(*e, preset.mFrame);
    e->mV->mLayer = LAYER_MAP_BG2;
    e->Data<CrackedData>().mTimer = FxSec(1.0f);
    return e;
}

Entity* Spawn_Hole(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kHole, preset, "hole", 16, 16, PART_VISUAL);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, false);
    Ecs_SetFrame(*e, preset.mFrame);
    e->mV->mLayer = LAYER_MAP_BG2;
    return e;
}

Entity* Spawn_SmallKeyGate(const MapEntity& preset, Entity&)
{
    return SpawnKeyGate(preset, GK_SMALL_KEY);
}

Entity* Spawn_CardGate(const MapEntity& preset, Entity&)
{
    return SpawnKeyGate(preset, GK_CARD);
}

Entity* Spawn_BigKeyGate(const MapEntity& preset, Entity&)
{
    return SpawnKeyGate(preset, GK_BIG_KEY);
}

Entity* Spawn_Key(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kKey, preset, "key");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrame(*e, preset.mFrame);
    if (strcmp(Preset_TypeValue(preset), "boss_rush") == 0)
    {
        e->Data<KeyData>().mBossRush = true;
        e->mV->mVisible = gGame.mEnemiesKilled > 1;
    }
    return e;
}

Entity* Spawn_HealthEntity(const MapEntity& preset, Entity&)
{
    return MakeHealth(Fx(preset.mX), Fx(preset.mY), preset.mFrame == 0, &preset);
}

Entity* Spawn_HealthDrop(fx x, fx y, bool big)
{
    return MakeHealth(x, y, big, nullptr);
}

Entity* Spawn_Explosion(const Entity& parent)
{
    Game_PlaySfx("hit_wall");
    Entity* e = Ecs_Create(kExplosion, parent.mT->mX - Fx(4), parent.mT->mY - Fx(4), PART_VISUAL);
    if (e)
    {
        Ecs_MarkSpawned(*e);
        Ecs_SetSolid(*e, false);
        Ecs_SetSheet(*e, "enemy_explode_2");
        e->mV->mLayer = LAYER_BG_ENTITIES;
        // (GlobalState.IsCell: the CELL's frames, 5 on, at 10 fps)
        Ecs_SetFrameBase(*e, Game_IsCell() ? 5 : 0);
        Ecs_Play(*e, "explode", true, Game_IsCell() ? 10 : 0);
    }
    return e;
}

Entity* Spawn_Checkpoint(const MapEntity& preset, Entity& player)
{
    Entity* e = Spawn_Basic(kCheckpoint, preset, "checkpoint");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, Game_IsCell() ? 4 : 0);
    e->mB->mW = e->mB->mH = 8;
    e->mV->mOffX = e->mV->mOffY = Fx(4);
    e->mT->mX += Fx(4);
    e->mT->mY += Fx(4);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    Ecs_SetSolid(*e, false);
    e->mV->mLayer = LAYER_VERY_BG_ENTITIES;
    Ecs_Play(*e, CheckpointActive(*e) ? "active" : "inactive");
    const bool on = Ecs_Intersects(Ecs_Hitbox(player), Ecs_Hitbox(*e));
    CheckpointEnter(*e, Event_Get("CheckpointTutorial") == 0 ? CPS_WAIT_TUTORIAL : (on ? CPS_SPAWNED_ON : CPS_WAIT));
    return e;
}

Entity* Spawn_SpringPad(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSpringPad, preset, "spring_pad");
    if (!e)
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_SetSolid(*e, false);
    JumpSetup(e->Data<JumpData>(), preset.mFrame);
    Ecs_Play(*e, "still");
    return e;
}

Entity* Spawn_JumpTrigger(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kJumpTrigger, Fx(preset.mX + 5), Fx(preset.mY + 12), PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 6;
    e->mB->mH = 4;
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, false);
    JumpSetup(e->Data<JumpData>(), preset.mFrame);
    return e;
}

Entity* Spawn_DungeonEntrance(const MapEntity& preset, Entity&)
{
    SetReturnTarget(preset.mX, preset.mY);
    return nullptr;
}

Entity* Spawn_DungeonEntranceRef(const MapEntity& preset, Entity&)
{
    // The content compiler put the linked entrance's position in frame / linkid.
    SetReturnTarget(preset.mFrame, preset.mLinkId);
    return nullptr;
}

Entity* Spawn_BossKeyReset(const MapEntity&, Entity&)
{
    // InventoryManager.SetMapKeys("BOSSRUSH", 0): the boss rush map isn't in the port.
    return nullptr;
}

void WhirlPool_DoTransition(Entity& pool)
{
    Event_Inc("fisherman.dead");
    Ecs_Play(pool, "transition");
}

Entity* Spawn_RedCaveEntrance(const MapEntity& preset, Entity&)
{
    const char* type = Preset_TypeValue(preset);
    int required = 1; // red_cave_l_ss, red_cave_r_ss
    if (strcmp(type, "red_cave_c_ss") == 0)
    {
        required = 0;
    }
    else if (strcmp(type, "red_cave_n_ss") == 0)
    {
        required = 2;
    }
    Entity* e = Spawn_Basic(kRedCaveEntrance, preset, "red_cave_left", 64, 64);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mOffX = Fx(4);
    e->mV->mOffY = Fx(32);
    e->mT->mX += Fx(4);
    e->mT->mY += Fx(32);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mB->mW = 56;
    e->mB->mH = 28;
    e->mB->mImmovable = true;
    Ecs_SetFrame(*e, 0);
    if (Event_Get(type) < required)
    {
        e->mExists = false;
        Spawn_DoorToggle(e->mT->mX, e->mT->mY, 56, 28);
    }
    return e;
}

Entity* Spawn_RedPillar(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kRedPillar, preset, "red_pillar", 16, 80);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 16;
    e->mB->mH = 80;
    e->mB->mImmovable = true;
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_Play(*e, "idle");
    RedPillarData& d = e->Data<RedPillarData>();
    d = RedPillarData();
    d.mChain = Ecs_Create(kChain, e->mT->mX, e->mT->mY + Fx(16), 0, nullptr, e);
    if (d.mChain)
    {
        d.mChain->mB->mW = d.mChain->mB->mH = 16;
    }
    return e;
}

Entity* Spawn_Door(const MapEntity& preset, Entity& player)
{
    const MapDoor* door = World_FindDoor(preset.mX, preset.mY);
    if (!door)
    {
        return nullptr; // OneWayDoor (a target only: exists = false) or a door with no linked door
    }
    const uint8_t type = door->mType;
    const bool interactable = type == DOOR_NEXUS_PAD || type == DOOR_NEXUS;
    const bool visible = type == DOOR_BLANK_PORTAL || type == DOOR_WHIRLPOOL || type == DOOR_NEXUS_PAD;
    const EntityClass& cls = interactable ? kNexusDoor : (type == DOOR_WHIRLPOOL ? kWhirlPool : kDoor);
    Entity* e = Ecs_Create(cls, Fx(door->mX), Fx(door->mY),
                           visible ? PART_VISUAL | PART_BRAIN : PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = door->mW;
    e->mB->mH = door->mH;
    e->mB->mImmovable = true;
    DoorData& d = e->Data<DoorData>();
    d = DoorData();
    d.mDoor = door;
    // The constructor's player_on_door: the base Door's hitbox, before the class resized or moved it (the frame size
    // for the doors with a sprite; BlankPortal and UpDoor redo it after CenterOffset)
    const Rect hit = Ecs_Hitbox(player);
    const int baseSize = type == DOOR_NEXUS_PAD ? 32 : 16;
    d.mPlayerOnDoor = Ecs_Intersects(hit, Rect{ preset.mX, preset.mY, baseSize, baseSize });
    if (type == DOOR_BLANK_PORTAL || type == 5)
    {
        d.mPlayerOnDoor = Ecs_Intersects(hit, Ecs_Hitbox(*e));
    }
    if (visible)
    {
        e->mV->mLayer = LAYER_BG_ENTITIES;
    }
    switch (type)
    {
    case DOOR_BLANK_PORTAL:
        Ecs_SetSheet(*e, "whiteportal");
        e->mV->mOffX = e->mV->mOffY = Fx(7);
        Ecs_Play(*e, Game_IsCell() ? "a@cell" : "a");
        break;
    case DOOR_WHIRLPOOL:
        Ecs_SetSheet(*e, "whirlpool");
        Ecs_Play(*e, Event_Get("fisherman.dead") != 0 ? "whirl_red" : "whirl");
        break;
    case DOOR_NEXUS_PAD:
        Ecs_SetSheet(*e, "nexus_pad");
        Ecs_SetFrameBase(*e, Game_IsCell() ? 2 : 0);
        e->mV->mOffX = Fx(6);
        e->mV->mOffY = Fx(4);
        Ecs_Play(*e, "off");
        gGame.mNexusPortals |= 1u << gGame.mCurrentMap; // ActivatedNexusPortals.Add(CURRENT_MAP_NAME)
        break;
    default:
        break;
    }
    return e;
}

const char* Gadgets_ProbeName(const Entity& e)
{
    if (e.mClass == &kKeyGate)
    {
        switch (const_cast<Entity&>(e).Data<KeyGateData>().mKind)
        {
        case GK_SMALL_KEY:
            return "SmallKeyGate";
        case GK_CARD:
            return "BigCardGate";
        default:
            return "BigKeyGate";
        }
    }
    if (e.mClass != &kDoor && e.mClass != &kNexusDoor && e.mClass != &kWhirlPool)
    {
        return nullptr;
    }
    switch (const_cast<Entity&>(e).Data<DoorData>().mDoor->mType)
    {
    case 1:
        return "DownDoor";
    case 4:
        return "BlankPortal";
    case 5:
        return "UpDoor";
    case 7:
        return "WhirlPool";
    case 8:
        return "FallDoor";
    case 9:
        return "NoMoveDoor";
    case 10:
        return "LeftDoor";
    case 11:
        return "RightDoor";
    case 12:
        return "WideDownDoor";
    case 13:
        return "TallLeftDoor";
    case 14:
        return "WideUpDoor";
    case 15:
        return "TallRightDoor";
    case 16:
        return "NexusPad";
    case 17:
        return "NexusDoor";
    default:
        return "Door";
    }
}

// ---- Propelled and Stop_Marker (Gadget/Propelled.cs) ------------------------------------------------------------
// A platform the player rides: a dust swept onto it powers it up, and landing on it (a jump) sends it off the way it
// faces until a Stop_Marker, then back to where it started.
namespace
{
struct PropelledData final
{
    bool mPressed;
    bool mPressedThisFrame;
    bool mActive;
    bool mStartDir;
    fx mInitX;
    fx mInitY;
    Entity* mPoof;
};

void PropelledUpdateFrame(Entity& self)
{
    const PropelledData& d = self.Data<PropelledData>();
    char anim[24];
    snprintf(anim, sizeof(anim), "%s-%s", d.mPressed ? "pressed" : "unpressed", d.mActive ? "active" : "inactive");
    Ecs_Play(self, anim);
}

void PropelledPotentialStop(Entity& self, fx x, fx y)
{
    PropelledData& d = self.Data<PropelledData>();
    Transform& t = *self.mT;
    if (t.mVelX == 0 && t.mVelY == 0)
    {
        return;
    }
    const fx dx = t.mX - self.mV->mOffX - x;
    const fx dy = t.mY - self.mV->mOffY - y;
    if (FxLengthSq(dx, dy) < Fx(4)) // (Vector2.LengthSquared)
    {
        t.mVelX = t.mVelY = 0;
        t.mX = x + self.mV->mOffX;
        t.mY = y + self.mV->mOffY;
        const Facing f = static_cast<Facing>(self.mFacing);
        Ecs_FaceTowards(self, t.mX - Fx(FacingX(f)), t.mY - Fx(FacingY(f)));
        d.mStartDir = !d.mStartDir;
    }
}

void PropelledUpdate(Entity& self)
{
    PropelledData& d = self.Data<PropelledData>();
    Ecs_BaseUpdate(self);
    if (d.mPressed && !d.mPressedThisFrame)
    {
        Game_PlaySfx("button_up");
    }
    else if (!d.mPressed && d.mPressedThisFrame)
    {
        Game_PlaySfx("button_down");
    }
    d.mPressed = d.mPressedThisFrame;
    PropelledUpdateFrame(self);
    if (!d.mStartDir)
    {
        PropelledPotentialStop(self, d.mInitX, d.mInitY);
    }
}

void PropelledPostUpdate(Entity& self)
{
    self.Data<PropelledData>().mPressedThisFrame = false;
}

const EntityClass kStopMarker = {
    "Stop_Marker", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, GROUP_STOP_MARKER, 0, 0,
};

void PropelledCollided(Entity& self, Entity& other)
{
    PropelledData& d = self.Data<PropelledData>();
    Transform& t = *self.mT;
    if (&other == World_Player())
    {
        Player_DontFall();
        if (Player_State() != PS_GROUND)
        {
            return;
        }
        d.mPressedThisFrame = true;
        if (Player_JustLanded() && d.mActive && t.mVelX == 0 && t.mVelY == 0)
        {
            const Facing f = static_cast<Facing>(self.mFacing);
            t.mVelX = Fx(FacingX(f) * 33);
            t.mVelY = Fx(FacingY(f) * 33);
            d.mActive = false;
            Entity& poof = *d.mPoof;
            poof.mExists = true;
            Ecs_Play(poof, "play", true);
            // the poof's middle 16 px behind the platform's visual centre
            poof.mT->mX = Ecs_VisualCenterX(self) - Fx(FacingX(f) * 16) - Fx(poof.mB->mW / 2);
            poof.mT->mY = Ecs_VisualCenterY(self) - Fx(FacingY(f) * 16) - Fx(poof.mB->mH / 2);
            Game_PlaySfx("dustpoof");
        }
        Player_AddAdditionalVel(t.mVelX, t.mVelY);
    }
    else if (other.mClass == &kDust)
    {
        if (!d.mActive)
        {
            d.mActive = true;
            Ecs_Play(other, "poof");
            Game_PlaySfx("dash_pad_2");
        }
    }
    else if (other.mClass == &kStopMarker && d.mStartDir)
    {
        PropelledPotentialStop(self, other.mT->mX, other.mT->mY);
    }
}

const EntityClass kPropelled = {
    "Propelled", PropelledUpdate, PropelledPostUpdate, PropelledCollided, nullptr, nullptr, nullptr, nullptr,
    0,           GROUP_PLAYER | GROUP_DUST | GROUP_STOP_MARKER, 0,
};

const EntityClass kPropelledPoof = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};
} // namespace

Entity* Spawn_Propelled(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kPropelled, preset, "moving_platform");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, preset.mFrame % 2 == 0 ? 4 : 0);
    e->mV->mLayer = LAYER_VERY_BG_ENTITIES;
    e->mB->mW = e->mB->mH = 10;
    Ecs_CenterOffset(*e);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    PropelledData& d = e->Data<PropelledData>();
    d = PropelledData();
    d.mStartDir = true;
    d.mInitX = Fx(preset.mX);
    d.mInitY = Fx(preset.mY);
    static const uint8_t kFacing[4] = { FACE_UP, FACE_RIGHT, FACE_DOWN, FACE_LEFT };
    e->mFacing = kFacing[preset.mFrame % 4];
    d.mActive = preset.mFrame >= 4;
    Entity* poof = Ecs_Create(kPropelledPoof, 0, 0, PART_VISUAL, nullptr, e);
    if (!poof)
    {
        return nullptr;
    }
    Ecs_SetSheet(*poof, "moving_platform_poof", 16, 16);
    poof->mB->mW = poof->mB->mH = 16;
    poof->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_KeepPooled(*poof);
    d.mPoof = poof;
    PropelledUpdateFrame(*e);
    return e;
}

Entity* Spawn_StopMarker(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kStopMarker, Fx(preset.mX), Fx(preset.mY), 0, &preset);
    if (e)
    {
        e->mB->mW = e->mB->mH = 16;
    }
    return e;
}

// ---- Elevator (Interactive/Elevator.cs) ------------------------------------------------------------------------------
// The doors open as the player walks up; stepping in opens the floor menu (ElevatorMenu, a ListSubstate), and a floor
// warps to that floor's elevator.
namespace
{
enum ElevatorState : uint8_t
{
    ELEV_CLOSE,
    ELEV_OPEN,
    ELEV_MENU,
    ELEV_LEAVING,
    ELEV_NONE, // PopState
};

constexpr int kMaxFloors = 4;

struct ElevatorData final
{
    uint8_t mState;
    // ElevatorMenu
    bool mMenuExit;
    int8_t mChosen;   // index into mFloors, -1: none
    uint8_t mSel;     // ListSubstate.state (kept from one ride to the next)
    uint8_t mLastSel; // _lastState
    uint8_t mFloorCount;
    const MapEntity* mFloors[kMaxFloors]; // the group's other elevators, by frame
    Entity* mMenuDetector;
    Entity* mOpenDetector;
};

struct DetectorData final
{
    bool mHit;
};

void DetectorUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    self.Data<DetectorData>().mHit = false;
}

void DetectorCollided(Entity& self, Entity&)
{
    self.Data<DetectorData>().mHit = true;
}

const EntityClass kPlayerDetector = {
    "PlayerDetector", DetectorUpdate, nullptr, DetectorCollided, nullptr, nullptr, nullptr, nullptr,
    0,                GROUP_PLAYER,   0,
};

const EntityClass kElevatorFrame = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

bool Detected(Entity* detector)
{
    return detector->Data<DetectorData>().mHit;
}

void ElevatorMenuDraw(const ElevatorData& d)
{
    // The C#'s lines are 1.3 font lines apart round the middle of its 136x126 panel at (15, 40) of the screen, which
    // is (15, 20) of the playfield.
    static char numbers[kMaxFloors][8];
    const char* lines[kMaxFloors + 2];
    int xs[kMaxFloors + 2];
    int ys[kMaxFloors + 2];
    const int count = d.mFloorCount + 2;
    const float lineHeight = 8 * 1.3f;
    const float yOffset = 126 / 2 - (lineHeight * static_cast<float>(count)) / 2;
    lines[0] = Dialog_GetFor("misc", "any", "elevator", 0);
    for (int i = 0; i < d.mFloorCount; i++)
    {
        snprintf(numbers[i], sizeof(numbers[i]), "%d", d.mFloors[i]->mFrame + 1);
        lines[i + 1] = numbers[i];
    }
    lines[count - 1] = Dialog_GetFor("misc", "any", "elevator", 1);
    for (int i = 0; i < count; i++)
    {
        const int w = lines[i] ? static_cast<int>(strlen(lines[i])) * 7 : 0;
        xs[i] = 15 + 68 - w / 2;
        ys[i] = 20 + static_cast<int>(yOffset + static_cast<float>(i) * lineHeight);
    }
    const int top = ys[0];
    const int sel = d.mSel + 1;
    Dialog_ShowPanel(15, 20, 136, 126);
    Dialog_ShowLabels(lines, xs, ys, count, top, xs[sel] - 7, ys[sel] + 2, true);
}

// The ElevatorMenu child state's frame (HandleInput, then ListSubstate.Update); true once it exits.
bool ElevatorMenuStep(Entity& self)
{
    ElevatorData& d = self.Data<ElevatorData>();
    const int options = d.mFloorCount + 1;
    if (key_hit(KEY_UP))
    {
        if (d.mSel > 0)
        {
            d.mSel--;
        }
    }
    else if (key_hit(KEY_DOWN))
    {
        if (d.mSel < options - 1)
        {
            d.mSel++;
        }
    }
    else if (key_hit(KEY_A))
    {
        // Select: the ActionOption's action
        d.mChosen = d.mSel < d.mFloorCount ? static_cast<int8_t>(d.mSel) : -1;
        d.mMenuExit = true;
        Game_PlaySfx("menu_select");
    }
    else if (key_hit(KEY_B))
    {
        d.mMenuExit = true; // ExitSubState
        Game_PlaySfx("menu_select");
    }
    const bool moved = d.mLastSel != d.mSel;
    if (moved)
    {
        d.mLastSel = d.mSel;
        Game_PlaySfx("menu_move");
    }
    if (d.mMenuExit)
    {
        Dialog_HideLabels();
        Dialog_HidePanel();
        return true;
    }
    // (drawn when it opens and when the selection moves: the text is rendered into sprites, which stay up)
    if (moved)
    {
        ElevatorMenuDraw(d);
    }
    return false;
}

void ElevatorEnter(Entity& self, uint8_t state)
{
    ElevatorData& d = self.Data<ElevatorData>();
    d.mState = state;
    switch (state)
    {
    case ELEV_CLOSE:
        if (Ecs_Frame(self) != 0)
        {
            Ecs_Play(self, "close");
            Game_PlaySfx("elevator_close");
        }
        break;
    case ELEV_OPEN:
        if (!Ecs_AnimIs(self, "open"))
        {
            Ecs_Play(self, "open");
            Game_PlaySfx("elevator_open");
        }
        break;
    case ELEV_MENU:
        d.mMenuExit = false;
        d.mLastSel = d.mSel; // GetControl
        Game_StartCutscene(self, ElevatorMenuStep);
        ElevatorMenuDraw(d);
        break;
    case ELEV_LEAVING:
        Ecs_Play(self, "open");
        Game_PlaySfx("elevator_open");
        break;
    default:
        break;
    }
}

void ElevatorUpdate(Entity& self)
{
    ElevatorData& d = self.Data<ElevatorData>();
    Ecs_BaseUpdate(self);
    // (the FSM's conditions in order, each seeing the state the one before left: a Close that opens can also go on to
    // the menu in the same frame, as RSG's AbstractState.Update runs them all)
    switch (d.mState)
    {
    case ELEV_CLOSE:
        if (Detected(d.mOpenDetector))
        {
            ElevatorEnter(self, ELEV_OPEN);
        }
        if (Detected(d.mMenuDetector))
        {
            ElevatorEnter(self, ELEV_MENU);
        }
        break;
    case ELEV_OPEN:
        if (!Detected(d.mOpenDetector))
        {
            ElevatorEnter(self, ELEV_CLOSE);
        }
        break;
    case ELEV_MENU:
        if (d.mMenuExit)
        {
            if (d.mChosen >= 0)
            {
                const MapEntity& next = *d.mFloors[d.mChosen];
                World_RequestWarp(gGame.mCurrentMap, next.mX + 8, next.mY + 30, 255);
                d.mState = ELEV_NONE;
            }
            else
            {
                ElevatorEnter(self, ELEV_LEAVING);
            }
        }
        break;
    case ELEV_LEAVING:
        if (!Detected(d.mMenuDetector))
        {
            ElevatorEnter(self, ELEV_OPEN);
        }
        break;
    default:
        break;
    }
}

const EntityClass kElevator = {
    "Elevator", ElevatorUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* MakeDetector(Entity& parent, int x, int y, int w, int h)
{
    Entity* e = Ecs_Create(kPlayerDetector, Fx(x), Fx(y), PART_BRAIN, nullptr, &parent);
    if (e)
    {
        e->mB->mW = static_cast<int16_t>(w);
        e->mB->mH = static_cast<int16_t>(h);
        Ecs_SetSolid(*e, false);
    }
    return e;
}
} // namespace

Entity* Spawn_Elevator(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kElevator, preset, "elevator_doors", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 32;
    ElevatorData& d = e->Data<ElevatorData>();
    d = ElevatorData();
    d.mChosen = -1;
    // The link group's others (every elevator of the map: one group a map), by frame.
    const MapEntityList& list = Entities_Presets();
    for (uint32_t i = 0; i < list.mCount && d.mFloorCount < kMaxFloors; i++)
    {
        const MapEntity& p = list.mEntities[i];
        if (p.mType == EPT_Elevator && &p != &preset)
        {
            int at = d.mFloorCount++;
            for (; at > 0 && d.mFloors[at - 1]->mFrame > p.mFrame; at--)
            {
                d.mFloors[at] = d.mFloors[at - 1];
            }
            d.mFloors[at] = &p;
        }
    }
    // SubEntities: menuDetector, openDetector, elevator_frame
    d.mMenuDetector = MakeDetector(*e, preset.mX, preset.mY, 32, 20);
    d.mOpenDetector = MakeDetector(*e, preset.mX, preset.mY + 32, 32, 32);
    Entity* frame = Ecs_Create(kElevatorFrame, Fx(preset.mX), Fx(preset.mY), PART_VISUAL, nullptr, e);
    if (!d.mMenuDetector || !d.mOpenDetector || !frame)
    {
        return nullptr;
    }
    Ecs_SetSheet(*frame, "elevator", 32, 32);
    Ecs_SetFrame(*frame, preset.mFrame);
    Ecs_SetSolid(*frame, false);
    ElevatorEnter(*e, ELEV_CLOSE);
    return e;
}

// ---- DashPad (Gadget/DashPad.cs): an arrow on the floor that dashes the player along it; dims for a while after
namespace
{
struct DashPadData final
{
    fx mDisabled;
};

void DashPadUpdate(Entity& self)
{
    DashPadData& d = self.Data<DashPadData>();
    Ecs_BaseUpdate(self);
    d.mDisabled += kFxTick;
    if (d.mDisabled > kFxOne)
    {
        self.mV->mOpacity = kFxOne;
        d.mDisabled = 0;
    }
}

void DashPadCollided(Entity& self, Entity& other)
{
    if (self.mV->mOpacity == kFxOne && Player_State() == PS_GROUND &&
        Ecs_Contains(Ecs_Hitbox(self), Ecs_CenterX(other), Ecs_CenterY(other)))
    {
        self.mV->mOpacity = FxF(0.5f);
        Player_Dash(static_cast<Facing>(self.mFacing));
    }
}

const EntityClass kDashPad = {
    "DashPad", DashPadUpdate, nullptr, DashPadCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};
} // namespace

namespace
{
// ---- GoDetector (GoDetector.cs): the GO tile puzzle. Solved once the BG tiles at its offsets read 60, 61, 62...
struct GoDetectorData final
{
    int8_t mChecks[8][2];
    uint8_t mCount;
};

void GoDetectorUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    const GoDetectorData& d = self.Data<GoDetectorData>();
    const int bx = FxFloor(self.mT->mX) / 16;
    const int by = FxFloor(self.mT->mY) / 16;
    for (int i = 0; i < d.mCount; i++)
    {
        if (World_GetTile(0, bx + d.mChecks[i][0], by + d.mChecks[i][1]) != 60 + i)
        {
            return;
        }
    }
    gGame.mPuzzlesSolved++;
    Preset_SetAlive(*self.mPreset, false);
    self.mExists = false;
}

const EntityClass kGoDetector = {
    "GoDetector", GoDetectorUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};
} // namespace

Entity* Spawn_GoDetector(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kGoDetector, preset, nullptr);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mVisible = false;
    Ecs_SetSolid(*e, false);
    // the type is "x,y;x,y;..."
    GoDetectorData& d = e->Data<GoDetectorData>();
    d = GoDetectorData();
    const char* p = Preset_TypeValue(preset);
    while (*p && d.mCount < 8)
    {
        int x = 0;
        int y = 0;
        if (sscanf(p, "%d,%d", &x, &y) != 2)
        {
            break;
        }
        d.mChecks[d.mCount][0] = static_cast<int8_t>(x);
        d.mChecks[d.mCount][1] = static_cast<int8_t>(y);
        d.mCount++;
        p = strchr(p, ';');
        if (!p)
        {
            break;
        }
        p++;
    }
    return e;
}

Entity* Spawn_DashPad(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kDashPad, preset, "dash_pads", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_SetFrame(*e, preset.mFrame + 4 + (Game_IsCell() ? 4 : 0));
    static const Facing kFacings[4] = { FACE_UP, FACE_RIGHT, FACE_DOWN, FACE_LEFT };
    e->mFacing = kFacings[preset.mFrame < 3 ? preset.mFrame : 3];
    e->Data<DashPadData>() = DashPadData();
    return e;
}
