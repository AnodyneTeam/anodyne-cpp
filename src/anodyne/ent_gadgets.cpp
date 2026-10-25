// Gadgets and events (AnodyneSharp Entities/Gadget, Entities/Events).
#include "entity_classes.h"

#include <stdlib.h>
#include <string.h>

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
            v.mScale += FxF(0.5f) / kFps;
            if (v.mScale >= kFxOne)
            {
                v.mScale = kFxOne;
                t.mVelX = t.mVelY = 0;
                d.mCardState++;
            }
            break;
        case 2:
            d.mTimer += kFxOne;
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
            v.mScale += FxF(3.0f) / kFps;
            v.mOpacity -= FxF(0.5f) / kFps;
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
        gGame.mEquippedBroom = BROOM_NORMAL;
    }
    if (gGame.mEquippedBroom == BROOM_NONE)
    {
        gGame.mEquippedBroom = type;
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

    Entity* item = Ecs_Create(kTreasure, chest.mT->mX, chest.mT->mY, PART_VISUAL | PART_BRAIN);
    if (item)
    {
        item->mB->mW = item->mB->mH = 16;
        Ecs_SetSolid(*item, false);
        Ecs_SetSheet(*item, sheet);
        Ecs_SetFrame(*item, frame);
        item->mV->mLayer = LAYER_FG_SPRITES;
        TreasureData& d = item->Data<TreasureData>();
        d.mKind = kind;
        d.mEndY = chest.mT->mY - Fx(16);
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
        if (gGame.mCardStatus[p.mLinkId])
        {
            if (item)
            {
                item->mExists = false; // EmptyTreasureEvent
            }
        }
        else
        {
            gGame.mCardStatus[p.mLinkId] = 1;
            Ecs_FireEvent(GEV_CHANGE_CARD_COUNT);
        }
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
    OpenTreasure(self);
    Ecs_SetFrame(self, Ecs_Frame(self) + 1);
    Preset_SetActivated(*self.mPreset, true);
    return true;
}

const EntityClass kChest = {
    "Treasure", nullptr, nullptr, ChestCollided, ChestInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER, ECF_INTERACTABLE,
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
    0,        GROUP_PLAYER | GROUP_SHIELDY, 0,
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
void DustCollided(Entity& self, Entity&)
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
    if (Ecs_AnimFinished(self) && (Ecs_AnimIs(self, "fallpoof") || Ecs_AnimIs(self, "poof")))
    {
        self.mExists = false;
    }
}

void DustMapEvent(Entity& self, MapEvent ev, uint8_t, fx, fx)
{
    // Dust.Fall: only dust just put down over a hole falls in.
    if (ev == MAPEV_FALL && Ecs_AnimIs(self, "unpoof") && Ecs_AnimFinished(self))
    {
        Ecs_Play(self, "fallpoof");
    }
}

void DustDestroy(Entity& self)
{
    Player_DustFreed(self);
}

const EntityClass kDust = {
    "Dust",     nullptr,    DustPostUpdate, DustCollided, nullptr, DustMapEvent, nullptr, DustDestroy,
    GROUP_DUST, GROUP_DUST, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

Entity* MakeDust(fx x, fx y, const MapEntity* preset)
{
    Entity* e = Ecs_Create(kDust, x, y, PART_VISUAL, preset);
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
    d.mTimer -= kFxOne;
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
}

const EntityClass kKeyGate = {
    "KeyGate", nullptr, nullptr, KeyGateCollided, nullptr, nullptr, nullptr, KeyGateDestroy, 0, 0, ECF_PART_OF_MAP,
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

// BigGate.OpeningSequence: five shakes 0.8 s apart, then the gate goes.
bool GateOpeningStep(Entity& gate)
{
    KeyGateData& d = gate.Data<KeyGateData>();
    if (!MoveTo(d.mTimer, FxF(0.8f), kFxOne))
    {
        return false;
    }
    d.mTimer = 0;
    if (d.mStep < 4)
    {
        d.mStep++;
        Game_PlaySfx("hit_ground_1");
        if (d.mStep == 0)
        {
            BreakLock(gate);
        }
        Ecs_SetFrame(gate, d.mStep);
        return false;
    }
    Game_PlaySfx("open");
    gate.mExists = false;
    return true;
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
            // (The C# first flies the key into the lock.)
            d.mStep = -1;
            d.mTimer = 0;
            Game_StartCutscene(gate, GateOpeningStep);
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
    d.mActivationTime -= kFxOne;
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
    self.Data<HealthData>().mLatency -= kFxOne;
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
    e->mB->mW = static_cast<int16_t>(big ? 16 : 10);
    e->mB->mH = 16;
    Ecs_Play(*e, "float");
    HealthData& d = e->Data<HealthData>();
    d.mHeal = big ? 3 : 1;
    d.mLatency = FxSec(0.5f);
    return e;
}

// ---- Explosion (Explosion.cs) -------------------------------------------------------------------------------------
void ExplosionUpdate(Entity& self)
{
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    Ecs_BaseUpdate(self);
}

const EntityClass kExplosion = {
    "Explosion", ExplosionUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
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
        d.mTimer -= kFxOne;
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
    Entity* e = Spawn_Basic(kChest, preset, "treasureboxes");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    int frame = 0;
    if (Preset_Activated(preset))
    {
        frame++;
        e->Data<ChestData>().mOpened = true;
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
    return MakeDust(x, y, nullptr);
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
        Ecs_SetSolid(*e, false);
        Ecs_SetSheet(*e, "enemy_explode_2");
        e->mV->mLayer = LAYER_BG_ENTITIES;
        Ecs_Play(*e, "explode");
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
