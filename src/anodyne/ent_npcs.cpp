// NPCs and decorations met before the first boss (AnodyneSharp Entities/Interactive, Decorations, Enemy/Etc).
#include "entity_classes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "credits.h"
#include "dialog.h"
#include "player.h"

namespace
{
bool IsPlayer(const Entity& e)
{
    return &e == World_Player();
}

fx DistanceToPlayer(const Entity& e)
{
    const Entity& p = *World_Player();
    return FxLength(p.mT->mX - e.mT->mX, p.mT->mY - e.mT->mY);
}

void SeparateFromPlayer(Entity& self, Entity& other)
{
    Ecs_Separate(other, self);
}

// The C#'s _flip on the "_l" anims (left reuses the right frames).
void FlipForLeft(Entity& e)
{
    const char* name = Ecs_AnimName(e);
    const size_t n = strlen(name);
    e.mV->mHFlip = n > 0 && name[n - 1] == 'l';
}

// MathUtilities.IntToString.
const char* NumberWord(int n)
{
    static const char* const kWords[] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
                                          "nine", "ten" };
    return (n >= 0 && n <= 10) ? kWords[n] : "one";
}

// ---- Sage (Sage.cs): walks up to the player once, talks; talks again when spoken to ------------------------------
enum SageKind : uint8_t
{
    SAGE_NEXUS,
    SAGE_OVERWORLD,
    SAGE_BEDROOM,
    SAGE_REDCAVE,
    SAGE_CROWD,
};

struct SageData final
{
    uint8_t mKind;
    uint8_t mStep; // the cutscene's
    bool mFacePlayer;
    fx mInitDistance;
    fx mStopDistance;
    const char* mScene;
};

const char* SageNexusScene()
{
    // (Skipped: "all_card_first", once every normal card of a map is in.)
    if (Event_Get("WindmillOpened") != 0)
    {
        return "after_windmill";
    }
    if (Game_BossDefeated("REDCAVE") && Game_BossDefeated("CROWD"))
    {
        return "before_windmill";
    }
    if (Game_BossDefeated("BEDROOM"))
    {
        return "after_bed";
    }
    if (Game_Visited("STREET"))
    {
        return "after_ent_str";
    }
    return "enter_nexus";
}

// Sage.StateLogic (+ SageOverworld's broom lesson).
bool SageCutscene(Entity& self)
{
    SageData& d = self.Data<SageData>();
    const Entity& p = *World_Player();
    switch (d.mStep)
    {
    case 0:
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(20));
        d.mStep++;
        // (the coroutine checks the distance straight away, before its first yield)
        // fall through
    case 1:
        if (DistanceToPlayer(self) > d.mStopDistance)
        {
            return false;
        }
        self.mT->mVelX = self.mT->mVelY = 0;
        Game_ShowCutsceneDialogue("sage", d.mScene);
        d.mStep++;
        return d.mKind != SAGE_OVERWORLD;
    case 2:
        // SageOverworld: swing the broom (the player may act, not move)...
        Player_SetActionsDisabled(false);
        if (Player_BroomOut())
        {
            d.mStep++;
        }
        return false;
    case 3:
        if (Player_BroomOut())
        {
            return false;
        }
        Game_ShowCutsceneDialogue("sage", "bedroom_entrance");
        d.mStep++;
        return true;
    default:
        return true;
    }
}

void SageUpdate(Entity& self)
{
    SageData& d = self.Data<SageData>();
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    Transform& t = *self.mT;
    if (d.mFacePlayer)
    {
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    }
    else if (t.mVelX != 0 || t.mVelY != 0)
    {
        Ecs_FaceTowards(self, t.mX + t.mVelX, t.mY + t.mVelY);
    }
    Ecs_PlayFacing(self, (t.mVelX == 0 && t.mVelY == 0) ? "idle" : "walk");
    FlipForLeft(self);

    if (!Preset_Activated(*self.mPreset) && !World_InTransition() && Player_State() == PS_GROUND &&
        DistanceToPlayer(self) < d.mInitDistance)
    {
        Preset_SetActivated(*self.mPreset, true);
        d.mStep = 0;
        Game_StartCutscene(self, SageCutscene);
    }
}

bool SageInteract(Entity& self, Facing)
{
    SageData& d = self.Data<SageData>();
    Game_ShowDialogue("sage", d.mKind == SAGE_NEXUS ? SageNexusScene() : d.mScene);
    return true;
}

void SageCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kSage = {
    "Sage", SageUpdate, nullptr, SageCollided, SageInteract, nullptr, nullptr, nullptr,
    0,      GROUP_PLAYER, ECF_INTERACTABLE,
};

Entity* SpawnSage(const MapEntity& preset, uint8_t kind, int init, int stop, const char* scene)
{
    Entity* e = Spawn_Basic(kSage, preset, "sage");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 10;
    e->mV->mOffX = e->mV->mOffY = Fx(3);
    e->mT->mX += Fx(3);
    e->mT->mY += Fx(3);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mB->mImmovable = true;
    SageData& d = e->Data<SageData>();
    d.mKind = kind;
    d.mFacePlayer = true;
    d.mInitDistance = Fx(init);
    d.mStopDistance = Fx(stop);
    d.mScene = scene;
    Ecs_Play(*e, "idle_d");
    return e;
}

// ---- SageTerminal (Sage.cs): waits by the big card gate, and opens it once all 36 cards are in --------------------
struct SageTerminalData final
{
    uint8_t mStep; // SageTerminal.States
};

// SageTerminal.States
void SageTerminalStep(Entity& self)
{
    SageTerminalData& d = self.Data<SageTerminalData>();
    switch (d.mStep)
    {
    case 0:
        if (DistanceToPlayer(self) > Fx(46))
        {
            return;
        }
        if (!Dialog_IsSceneDirty("sage", "entrance"))
        {
            Game_ShowDialogue("sage", "entrance");
            d.mStep = 1;
            return;
        }
        d.mStep = 2;
        // fall through
    case 1:
        if (d.mStep == 1)
        {
            if (!Game_DialogueFinished())
            {
                return;
            }
            d.mStep = 2;
        }
        // fall through
    case 2:
        if (Inventory_CardCount() < 36)
        {
            d.mStep = 5;
            return;
        }
        Game_ShowDialogue("sage", "etc", 0);
        d.mStep = 3;
        return;
    case 3:
        if (!Game_DialogueFinished())
        {
            return;
        }
        Ecs_FireEvent(GEV_CHANGE_CARD_COUNT); // ChangeCardCount(92)
        d.mStep = 4;
        // fall through
    case 4:
        if (World_Player()->mT->mY > Fx(gGame.mGridY * 160 + 55))
        {
            return;
        }
        Game_ShowDialogue("sage", "etc", 1);
        Preset_SetAlive(*self.mPreset, false);
        d.mStep = 5;
        return;
    default:
        return;
    }
}

void SageTerminalUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    const Transform& t = *self.mT;
    Ecs_PlayFacing(self, (t.mVelX == 0 && t.mVelY == 0) ? "idle" : "walk");
    FlipForLeft(self);
    SageTerminalStep(self);
}

bool SageTerminalInteract(Entity&, Facing)
{
    const int cards = Inventory_CardCount();
    if (cards < 18)
    {
        Game_ShowDialogue("sage", "entrance", 1);
    }
    else if (cards < 36)
    {
        Game_ShowDialogue("sage", "entrance", 2);
    }
    else
    {
        Game_ShowText("...");
    }
    return true;
}

const EntityClass kSageTerminal = {
    "SageTerminal", SageTerminalUpdate, nullptr, SageCollided, SageTerminalInteract, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,       ECF_INTERACTABLE,
};

// ---- Fisherman ------------------------------------------------------------------------------------------------------
struct FishermanData final
{
    int mHealth;
    bool mDead;
};

void FishermanUpdate(Entity& self)
{
    FishermanData& d = self.Data<FishermanData>();
    Ecs_BaseUpdate(self);
    if (d.mDead)
    {
        // (sinks into the whirlpool, shrinking)
        Visual& v = *self.mV;
        MoveTo(v.mYPush, Fx(16), FxF(16 / 0.27f));
        MoveTo(v.mOffY, Fx(14), FxF(16 / 0.27f));
        MoveTo(v.mOffX, Fx(-4), FxF(16 / 0.27f));
        MoveTo(v.mScale, FxF(0.70f), FxF(0.40f / 0.27f));
        if (v.mYPush == Fx(16))
        {
            self.mExists = false;
        }
    }
}

bool FishermanCutscene(Entity& self)
{
    // CoroutineUtils.WaitFor(() => dead)
    return self.Data<FishermanData>().mDead;
}

void FishermanCollided(Entity& self, Entity& other)
{
    FishermanData& d = self.Data<FishermanData>();
    if (other.mClass->mGroups & GROUP_BROOM)
    {
        if (d.mHealth > 0 && !self.mV->mFlickering)
        {
            Ecs_Flicker(self, FxSec(0.5f));
            d.mHealth--;
            if (d.mHealth <= 0)
            {
                self.mT->mVelY = Fx(17);
                Ecs_Play(self, "dead");
                Preset_SetAlive(*self.mPreset, false);
                Game_StartCutscene(self, FishermanCutscene);
            }
            return;
        }
    }
    else if (other.mClass->mGroups & GROUP_WHIRLPOOL)
    {
        if (!d.mDead && self.mT->mY >= Fx(self.mPreset->mY + 28))
        {
            d.mDead = true;
            self.mT->mVelX = 0;
            self.mT->mVelY = 0;
            WhirlPool_DoTransition(other);
            return;
        }
    }
    Ecs_Separate(self, other);
}

const EntityClass kFisherman = {
    "Fisherman", FishermanUpdate, nullptr, FishermanCollided, nullptr, nullptr, nullptr, nullptr, 0,
    GROUP_PLAYER | GROUP_BROOM | GROUP_WHIRLPOOL, 0,
};

// ---- Red Sea: RedWalker, BombDude, HairDude, Sign -----------------------------------------------------------------
void RedWalkerUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (self.mB->mTouching != TOUCH_NONE)
    {
        self.mT->mVelX = -self.mT->mVelX;
        self.mT->mVelY = -self.mT->mVelY;
        self.mV->mHFlip = !self.mV->mHFlip;
    }
}

void RedWalkerCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
    self.mB->mTouching = TOUCH_NONE;
}

const EntityClass kRedWalker = {
    "RedWalker", RedWalkerUpdate, nullptr, RedWalkerCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER,
    ECF_KEEP_ON_SCREEN,
};

bool BombDudeInteract(Entity& self, Facing)
{
    if (Dialog_IsSceneFinished("generic_npc", "bomb"))
    {
        Spawn_Explosion(self);
        Player_ReceiveDamage(1, true, true);
        self.mExists = false;
        Preset_SetAlive(*self.mPreset, false);
    }
    else
    {
        Game_ShowDialogue("generic_npc", "bomb");
    }
    return true;
}

void SeparateSelfFirst(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kBombDude = {
    "BombDude", nullptr, nullptr, SeparateSelfFirst, BombDudeInteract, nullptr, nullptr, nullptr, 0, GROUP_PLAYER,
    ECF_INTERACTABLE,
};

// QuestNPCHelper.NeedsSecond: a quest NPC says its second line once its first is done and a boss has fallen since
// (or six have, or the second was already seen).
bool NeedsSecond(const char* name, const char* normal, const char* second)
{
    char event[48];
    snprintf(event, sizeof(event), "%s-QuestCounter", name);
    const int previous = Event_Get(event);
    const int current = __builtin_popcount(gGame.mBossDefeated);
    if (Dialog_IsSceneDirty("generic_npc", normal))
    {
        const bool seenSecond = Dialog_IsSceneDirty("generic_npc", second);
        const bool finishedFirst = Dialog_IsSceneFinished("generic_npc", normal);
        if (finishedFirst && (current >= 6 || current > previous || seenSecond))
        {
            Event_Set(event, current);
            return true;
        }
        return false;
    }
    Event_Set(event, current);
    return false;
}

void HairDudeUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    const Entity& p = *Player_Entity();
    Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    Ecs_PlayFacing(self, "walk");
}

bool HairDudeInteract(Entity&, Facing)
{
    Game_ShowDialogue("generic_npc", NeedsSecond("hairdude", "first", "second") ? "second" : "first");
    return true;
}

const EntityClass kHairDude = {
    "HairDude", HairDudeUpdate, nullptr, SeparateSelfFirst, HairDudeInteract, nullptr, nullptr, nullptr, 0,
    GROUP_PLAYER, ECF_INTERACTABLE,
};

bool SignInteract(Entity& self, Facing)
{
    Game_ShowDialogueFor("misc", "any", "solidsprite", self.mPreset->mFrame - 2);
    return true;
}

const EntityClass kSign = {
    "Sign", nullptr, nullptr, SeparateSelfFirst, SignInteract, nullptr, nullptr, nullptr, 0, 0,
    ECF_PART_OF_MAP | ECF_INTERACTABLE,
};

// ---- Rock / Statue / Sadbro / DungeonStatue -----------------------------------------------------------------------
bool RockInteract(Entity& self, Facing)
{
    const char* text = Dialog_Get("rock", NumberWord(self.mPreset->mFrame + 1));
    if (Event_Get("RockTalk") == 0)
    {
        // The first rock also explains what rocks are.
        static char sJoined[512];
        char rock[384];
        snprintf(rock, sizeof(rock), "%s", text ? text : "");
        const char* intro = Dialog_GetFor("misc", "any", "rock", 0);
        snprintf(sJoined, sizeof(sJoined), "%s^\n%s", intro ? intro : "", rock);
        Event_Inc("RockTalk");
        Game_ShowText(sJoined);
    }
    else if (text)
    {
        Game_ShowText(text);
    }
    return true;
}

const EntityClass kRock = {
    "Rock", nullptr, nullptr, SeparateFromPlayer, RockInteract, nullptr, nullptr, nullptr,
    0,      GROUP_PLAYER, ECF_INTERACTABLE,
};

bool StatueInteract(Entity&, Facing)
{
    Game_ShowDialogue("statue", "one");
    return true;
}

const EntityClass kStatue = {
    "Statue", nullptr, nullptr, SeparateFromPlayer, StatueInteract, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER, ECF_INTERACTABLE,
};

bool SadbroInteract(Entity& self, Facing)
{
    const char* scene = "initial_forced";
    if (Preset_Activated(*self.mPreset))
    {
        if (Game_BossDefeated("BEDROOM"))
        {
            scene = "bedroom_done";
        }
        else if (Game_Visited("BEDROOM"))
        {
            scene = "bedroom_not_done";
        }
    }
    Game_ShowDialogue("sadbro", scene);
    Preset_SetActivated(*self.mPreset, true);
    return true;
}

const EntityClass kSadbro = {
    "Sadbro", nullptr, nullptr, SeparateFromPlayer, SadbroInteract, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER, ECF_INTERACTABLE,
};

bool DungeonStatueInteract(Entity&, Facing)
{
    Game_ShowDialogue("dungeon_statue", Event_Get("WindmillOpened") != 0 ? "two" : "one");
    return true;
}

void DungeonStatueCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kDungeonStatue = {
    "DungeonStatue", nullptr, nullptr, DungeonStatueCollided, DungeonStatueInteract, nullptr, nullptr, nullptr,
    0,               0,       ECF_PART_OF_MAP | ECF_INTERACTABLE,
};

// ---- Eye_Light (Eye_Light.cs) -------------------------------------------------------------------------------------
const EntityClass kEyeLight = {
    "EyeLight", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kGlowLight = {
    "GlowLight", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBrightGlowLight = {
    "BrightGlowLight", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- BedRoomBounceLight (Lights/Light.cs) ------------------------------------------------------------------------
// The Bedroom's light: bounces round the screen, then after 2 s locks on to the player once it touches them.
struct BounceLightData final
{
    fx mLockOnTimer;
    bool mLockedOn;
};

void BounceLightUpdate(Entity& self)
{
    BounceLightData& d = self.Data<BounceLightData>();
    Transform& t = *self.mT;
    if (d.mLockedOn)
    {
        const Entity& p = *World_Player();
        MoveTo(t.mX, p.mT->mX, Fx(30));
        MoveTo(t.mY, p.mT->mY, Fx(30));
    }
    else
    {
        if (self.mB->mTouching & (TOUCH_LEFT | TOUCH_RIGHT))
        {
            t.mVelX = -t.mVelX;
        }
        if (self.mB->mTouching & (TOUCH_UP | TOUCH_DOWN))
        {
            t.mVelY = -t.mVelY;
        }
        t.mVelX += Rng_Unit() * 2 - kFxOne;
        t.mVelY += Rng_Unit() * 2 - kFxOne;
        d.mLockOnTimer -= kFxTick;
    }
    Ecs_BaseUpdate(self);
}

void BounceLightCollided(Entity& self, Entity&)
{
    BounceLightData& d = self.Data<BounceLightData>();
    if (d.mLockOnTimer < 0)
    {
        d.mLockedOn = true;
    }
}

const EntityClass kBounceLight = {
    "BedRoomBounceLight", BounceLightUpdate, nullptr, BounceLightCollided, nullptr, nullptr, nullptr, nullptr,
    0,                    GROUP_PLAYER,      ECF_KEEP_ON_SCREEN,
};

// ---- FollowerBro (FollowerBro.cs) ---------------------------------------------------------------------------------
enum BroState : uint8_t
{
    BRO_HIDDEN,
    BRO_APPEARING,
    BRO_WALK,
    BRO_WAIT,
    BRO_DISAPPEARING,
};

struct BroData final
{
    uint8_t mState;
    bool mTouched;
};

void BroUpdate(Entity& self)
{
    BroData& d = self.Data<BroData>();
    const Entity& p = *World_Player();
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    switch (d.mState)
    {
    case BRO_HIDDEN:
        if (p.mT->mY - t.mY < -Fx(20))
        {
            d.mState = BRO_APPEARING;
            Player_SetDontMove(true);
            Preset_SetAlive(*self.mPreset, false);
        }
        break;
    case BRO_APPEARING:
        v.mOpacity += FxStep(FxF(0.6f));
        if (v.mOpacity > FxF(0.96f))
        {
            d.mState = BRO_WALK;
            Player_SetDontMove(false);
            Ecs_Play(self, "walk");
        }
        break;
    case BRO_WALK:
        t.mVelX = p.mT->mX > t.mX ? Fx(10) : -Fx(10);
        t.mVelY = -Fx(10);
        if (d.mTouched)
        {
            d.mState = BRO_DISAPPEARING;
        }
        else if (p.mT->mY >= t.mY)
        {
            d.mState = BRO_WAIT;
        }
        break;
    case BRO_WAIT:
        if (p.mT->mY < t.mY)
        {
            d.mState = BRO_WALK;
        }
        break;
    default:
        v.mOpacity -= FxStep(FxF(0.6f));
        if (v.mOpacity <= 0)
        {
            self.mExists = false;
        }
        break;
    }
    d.mTouched = false;
    Ecs_BaseUpdate(self);
}

void BroCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        self.Data<BroData>().mTouched = true;
    }
}

const EntityClass kFollowerBro = {
    "FollowerBro", BroUpdate, nullptr, BroCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- HealthCicada (HealthCicada.cs + HealthCicadaSentinel.cs) ---------------------------------------------------
// The sentinel is the preset (an invisible 16x16 box); the cicada and its gnaw particles are its sub-entities, whose
// positions are the C#'s UI-space ones (the cicada starts at UI -16,30), drawn in game coordinates through their
// draw offsets. The cicada's state logic runs in the sentinel's update, as the C#'s _child.Update() does.
enum CicadaStep : uint8_t
{
    CICADA_WAIT_FOR_BOSS,
    CICADA_FLY_TO_START,
    CICADA_WAIT_FOR_PLAYER,
    CICADA_FLY_AWAY,
    CICADA_HEAL,
    CICADA_FLY_TO_HEALTH,
    CICADA_GNAW,
    CICADA_FADE,
    CICADA_WAIT_FOR_BOXES,
    CICADA_DONE,
};

constexpr int kCicadaBoxes = 10;

struct CicadaData final
{
    uint8_t mStep;
    uint8_t mGnaws;
    bool mPlayerCollided;
    bool mActivated;
    bool mPlayerAway;
    bool mChirp;
    bool mGameCoords; // drawInGameCoordinates
    fx mTimer;
    fx mTargetX;
    fx mTargetY;
    fx mFlyStartY; // UI y when it set off for the health bar (its draw offset eases from the header's 20 px to 0)
    Entity* mCicada;
    Entity* mBoxes[kCicadaBoxes];
};

bool CicadaHealStep(Entity& self)
{
    // The cutscene only waits for the preset to die (HealthCicada's own states run in its Update).
    return !Preset_Alive(*self.mPreset);
}

void CicadaBoxUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (MoveTo(self.mV->mOpacity, 0, FxF(0.3f)))
    {
        self.mExists = false;
    }
}

const EntityClass kCicadaBox = {
    "HealthCicadaBox", CicadaBoxUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kCicadaFly = {
    "HealthCicada", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// UI space to the screen: drawn at the room's top-left less the 20 px header, then (as UI) over the health bar.
void CicadaPlaceDrawing(Entity& self)
{
    CicadaData& d = self.Data<CicadaData>();
    Entity& c = *d.mCicada;
    if (d.mGameCoords)
    {
        c.mV->mOffX = -Fx(gGame.mGridX * 160);
        c.mV->mOffY = -Fx(gGame.mGridY * 160 - 20);
    }
    else
    {
        // (the C#'s UI has the header above the playfield; the port's health bar is drawn on it)
        fx header = Fx(20);
        const fx span = d.mFlyStartY - d.mTargetY;
        if (d.mStep >= CICADA_FLY_TO_HEALTH)
        {
            header = span == 0 ? 0 : FxMul(Fx(20), FxDiv(c.mT->mY - d.mTargetY, span));
            header = header < 0 ? 0 : (header > Fx(20) ? Fx(20) : header);
        }
        c.mV->mOffX = -World_CamX();
        c.mV->mOffY = -(World_CamY() - header);
    }
    for (Entity* box : d.mBoxes)
    {
        box->mV->mOffX = -World_CamX();
        box->mV->mOffY = -World_CamY();
    }
}

void CicadaUpdate(Entity& self)
{
    CicadaData& d = self.Data<CicadaData>();
    Entity& c = *d.mCicada;
    Transform& t = *c.mT;
    Visual& v = *c.mV;
    const Entity& p = *World_Player();
    Ecs_BaseUpdate(self);
    if (!d.mPlayerAway && d.mActivated &&
        FxLength(p.mT->mX - self.mT->mX, p.mT->mY - self.mT->mY) > Fx(80))
    {
        d.mPlayerAway = true;
        d.mActivated = false;
    }
    // _child.Update(): the chirp, then its StateLogic until the next yield (each case runs on into the next).
    if (d.mChirp)
    {
        Game_PlaySfx("cicada_chirp");
    }
    switch (d.mStep)
    {
    case CICADA_WAIT_FOR_BOSS:
        if (Game_IsDungeon() && !Game_BossDefeated(gGame.mMapName))
        {
            break;
        }
        d.mTargetX = self.mT->mX - Fx(gGame.mGridX * 160);
        d.mTargetY = self.mT->mY - Fx(gGame.mGridY * 160) + Fx(20);
        v.mVisible = true;
        Ecs_Play(c, "fly");
        d.mChirp = true;
        d.mStep = CICADA_FLY_TO_START;
        [[fallthrough]];
    case CICADA_FLY_TO_START:
    {
        const bool x = MoveTo(t.mX, d.mTargetX, Fx(30));
        const bool y = MoveTo(t.mY, d.mTargetY, Fx(30));
        if (!(x && y))
        {
            break;
        }
        d.mActivated = true;
        Ecs_Play(c, "idle");
        d.mStep = CICADA_WAIT_FOR_PLAYER;
    }
        [[fallthrough]];
    case CICADA_WAIT_FOR_PLAYER:
        if (!d.mPlayerCollided)
        {
            if (!d.mPlayerAway)
            {
                break;
            }
            Ecs_Play(c, "fly");
            t.mVelY = -Fx(100);
            d.mStep = CICADA_FLY_AWAY;
            goto fly_away;
        }
        v.mLayer = LAYER_UI;
        d.mGameCoords = false;
        d.mTimer = 0;
        Game_StartCutscene(self, CicadaHealStep);
        Ecs_Play(c, "fly");
        d.mStep = CICADA_HEAL;
        [[fallthrough]];
    case CICADA_HEAL:
        if (gGame.mCurHealth < gGame.mMaxHealth)
        {
            d.mTimer += kFxTick;
            if (d.mTimer > FxSec(0.5f))
            {
                d.mTimer = 0;
                Game_PlaySfx("get_small_health");
                GameState_SetHealth(gGame.mCurHealth + 1);
            }
            break;
        }
        // HealthBar.GetHealthPiecePos(MAX_HEALTH), less 4.
        d.mTargetX = Fx(155 - 11 - 8 * (7 - gGame.mMaxHealth % 8) - 7 * (gGame.mMaxHealth / 8) - 4);
        d.mTargetY = Fx(2 + gGame.mMaxHealth / 8 * 7 - 4);
        d.mFlyStartY = t.mY;
        d.mChirp = false;
        d.mStep = CICADA_FLY_TO_HEALTH;
        [[fallthrough]];
    case CICADA_FLY_TO_HEALTH:
    {
        const bool x = MoveTo(t.mX, d.mTargetX, Fx(30));
        const bool y = MoveTo(t.mY, d.mTargetY, Fx(30));
        if (!(x && y))
        {
            break;
        }
        Ecs_Play(c, "gnaw");
        d.mGnaws = 0;
        d.mTimer = FxSec(2.0f); // the first one is instant
        d.mStep = CICADA_GNAW;
    }
        [[fallthrough]];
    case CICADA_GNAW:
        if (d.mGnaws < 3)
        {
            d.mTimer += kFxTick;
            if (d.mTimer > FxSec(1.7f))
            {
                d.mTimer = 0;
                d.mGnaws++;
                Game_PlaySfx("4sht_pop");
                for (Entity* box : d.mBoxes)
                {
                    // BoxFX.Spawn
                    box->mExists = true;
                    box->mT->mX = box->mT->mLastX = t.mX + Fx(6);
                    box->mT->mY = box->mT->mLastY = t.mY + Fx(6);
                    box->mV->mOpacity = kFxOne;
                    box->mT->mAccY = Fx(Rng_Range(50, 71));
                    box->mT->mVelX = Fx(Rng_Range(-40, 81));
                    box->mT->mVelY = Fx(Rng_Range(10, 31));
                }
            }
            break;
        }
        GameState_SetMaxHealth(gGame.mMaxHealth + 1);
        GameState_SetHealth(gGame.mCurHealth + 1);
        Ecs_Play(c, "fly");
        t.mVelY = -Fx(40);
        d.mStep = CICADA_FADE;
        [[fallthrough]];
    case CICADA_FADE:
        if (!MoveTo(v.mOpacity, 0, FxF(0.6f)))
        {
            break;
        }
        Preset_SetAlive(*self.mPreset, false);
        d.mStep = CICADA_WAIT_FOR_BOXES;
        [[fallthrough]];
    case CICADA_WAIT_FOR_BOXES:
        for (const Entity* box : d.mBoxes)
        {
            if (box->mExists)
            {
                goto yield;
            }
        }
        self.mExists = false;
        d.mStep = CICADA_DONE;
        break;
    case CICADA_FLY_AWAY:
    fly_away:
        if (!MoveTo(v.mOpacity, 0, FxF(1.2f)))
        {
            break;
        }
        // (the sentinel goes until the screen comes back)
        self.mExists = false;
        d.mStep = CICADA_DONE;
        break;
    default:
        break;
    }
yield:
    CicadaPlaceDrawing(self);
}

void CicadaCollided(Entity& self, Entity&)
{
    CicadaData& d = self.Data<CicadaData>();
    if (d.mActivated && !d.mPlayerCollided && Player_State() == PS_GROUND)
    {
        d.mPlayerCollided = true;
        d.mActivated = false;
    }
}

const EntityClass kCicada = {
    "HealthCicadaSentinel", CicadaUpdate, nullptr, CicadaCollided, nullptr, nullptr, nullptr, nullptr, 0,
    GROUP_PLAYER,           0,
};

// ---- Big_Key (Big_Key.cs) -----------------------------------------------------------------------------------------
struct BigKeyData final
{
    uint8_t mStep;
    fx mRadius;
    fx mSparkleTimer;
    fx mSparkleMax;
    Entity* mSparkles[3]; // its EntityPool<Sparkle>
};

void SparklePostUpdate(Entity& self)
{
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
}

const EntityClass kSparkle = {
    "KeySparkle", nullptr, SparklePostUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void BigKeyUpdate(Entity& self)
{
    BigKeyData& d = self.Data<BigKeyData>();
    Ecs_BaseUpdate(self);
    d.mSparkleTimer += kFxTick;
    if (d.mSparkleTimer > d.mSparkleMax)
    {
        d.mSparkleTimer = 0;
        for (Entity* s : d.mSparkles)
        {
            if (s->mExists)
            {
                continue;
            }
            // Sparkle.Spawn (a reused one keeps its velocity, as in the C#)
            s->mExists = true;
            Ecs_Play(*s, "sparkle");
            s->mT->mX = self.mT->mX - Fx(2);
            s->mT->mY = self.mT->mY - Fx(3) - self.mV->mOffY;
            s->mT->mX += FxMul(Fx(self.mB->mW + 4), Rng_Unit());
            s->mT->mY += FxMul(Fx(self.mB->mH + 6), Rng_Unit());
            if (!Ecs_Solid(self))
            {
                const fx faster = d.mSparkleMax - FxSec(0.06f);
                d.mSparkleMax = faster > FxSec(0.15f) ? faster : FxSec(0.15f);
                s->mT->mVelY = Fx(20);
                static const char* const kSfx[5] = { "sparkle_1", "sparkle_1", "sparkle_2", "sparkle_2",
                                                     "sparkle_3" };
                Game_PlaySfxOneOf(kSfx, 5);
            }
            break;
        }
    }
}

// MathUtilities.RotateAround: the key's direction from the player's centre turns by speed * dt (Matrix.CreateRotationZ),
// at the given radius.
void BigKeyOrbit(Entity& self, fx angularSpeed)
{
    const Entity& p = *World_Player();
    const float cx = Ecs_CenterX(p).mV;
    const float cy = Ecs_CenterY(p).mV;
    float dx = self.mT->mX.mV - cx;
    float dy = self.mT->mY.mV - cy;
    const float inv = 1.0f / sqrtf(dx * dx + dy * dy);
    dx *= inv;
    dy *= inv;
    const double angle = static_cast<double>(FxStep(angularSpeed).mV);
    const float c = static_cast<float>(cos(angle));
    const float sn = static_cast<float>(sin(angle));
    const float rx = dx * c + dy * -sn;
    const float ry = dx * sn + dy * c;
    const float r = self.Data<BigKeyData>().mRadius.mV;
    self.mT->mX = fx(cx + rx * r);
    self.mT->mY = fx(cy + ry * r);
}

// Big_Key.States: each step runs on into the next in the same frame, as the C# loop does until its next yield.
bool BigKeyCutscene(Entity& self)
{
    BigKeyData& d = self.Data<BigKeyData>();
    Visual& v = *self.mV;
    switch (d.mStep)
    {
    case 0:
    {
        const Entity& p = *World_Player();
        d.mRadius = FxLength(self.mT->mX - Ecs_CenterX(p), self.mT->mY - Ecs_CenterY(p));
        d.mStep = 1;
    }
        [[fallthrough]];
    case 1:
    {
        const bool a = MoveTo(v.mOffY, Fx(16), Fx(18));
        const bool b = MoveTo(d.mRadius, Fx(40), Fx(12));
        if (!(a && b))
        {
            BigKeyOrbit(self, FxF(3.6f));
            return false;
        }
        d.mStep = 2;
    }
        [[fallthrough]];
    case 2:
    {
        const bool a = MoveTo(v.mOffY, Fx(64), Fx(18));
        const bool b = MoveTo(d.mRadius, Fx(2), FxF(8.4f));
        if (!(a && b))
        {
            BigKeyOrbit(self, FxF(6.2f));
            return false;
        }
        d.mStep = 3;
    }
        [[fallthrough]];
    case 3:
        if (!MoveTo(v.mOffY, Fx(70), Fx(3)))
        {
            return false;
        }
        d.mStep = 4;
        [[fallthrough]];
    case 4:
        if (!MoveTo(v.mOffY, Fx(16), Fx(132)))
        {
            return false;
        }
        d.mStep = 5;
        [[fallthrough]];
    default:
        Screen_Flash(Fx(2));
        Screen_Shake(FxF(0.02f), FxF(0.4f));
        Game_PlaySfx("sun_guy_death_long");
        self.mExists = false;
        return true;
    }
}

bool BigKeyInteract(Entity& self, Facing)
{
    BigKeyData& d = self.Data<BigKeyData>();
    Preset_SetAlive(*self.mPreset, false);
    Ecs_SetSolid(self, false);
    gGame.mBigKeyStatus[(Ecs_Frame(self) / 2) % 3] = true;
    d.mStep = 0;
    Game_StartCutscene(self, BigKeyCutscene);
    return true;
}

void BigKeyCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kBigKey = {
    "BigKey", BigKeyUpdate, nullptr, BigKeyCollided, BigKeyInteract, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER, ECF_INTERACTABLE,
};

// ---- MitraOverworld (Mitra.cs) ------------------------------------------------------------------------------------
enum MitraStep : uint8_t
{
    MITRA_INITIAL_WAIT,
    MITRA_ENTRANCE,
    MITRA_SECOND_WAIT,
    MITRA_EXIT,
};

struct MitraData final
{
    uint8_t mState;
    uint8_t mStep; // within the entrance / exit cutscenes
    bool mFacePlayer;
    bool mOnBike;
    bool mFields; // MitraFields (else MitraOverworld)
    bool mCliff;  // MitraCliff: rides in to give the jump shoes
    bool mPlayerJumped;
    bool mExiting;
    bool mInitial;
    bool mGivenQuestHint;
    bool mGivenInitialHint; // only give the dungeon/windmill hint once
    Entity* mBike;
    // its VolumeEvent: fades the song to mVolTarget at 0.4 a second
    float mVolTarget;
    bool mVolReached;
};

void MitraOnBike(Entity& self, bool on)
{
    MitraData& d = self.Data<MitraData>();
    d.mOnBike = on;
    if (on)
    {
        Ecs_SetSheet(self, "mitra_bike", 20, 20);
        self.mB->mW = self.mB->mH = 10;
        self.mV->mOffX = self.mV->mOffY = Fx(5);
    }
    else
    {
        Ecs_SetSheet(self, "mitra", 16, 16);
        self.mB->mW = self.mB->mH = 16;
        self.mV->mOffX = self.mV->mOffY = 0;
    }
}

void MitraFacePlayerPose(Entity& self)
{
    Transform& t = *self.mT;
    const Entity& p = *World_Player();
    if (t.mVelX == 0 && t.mVelY == 0)
    {
        if (self.Data<MitraData>().mFacePlayer)
        {
            Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        }
        Ecs_PlayFacing(self, "idle");
    }
    else
    {
        Ecs_FaceTowards(self, t.mX + t.mVelX, t.mY + t.mVelY);
        Ecs_PlayFacing(self, "walk");
    }
    FlipForLeft(self);
}

bool MitraEntrance(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    Transform& t = *self.mT;
    Entity& p = *World_Player();
    const fx left = Fx(gGame.mGridX * 160);
    switch (d.mStep)
    {
    case 0:
        t.mX -= Fx(20);
        Game_PlaySong("mitra");
        Preset_SetAlive(*self.mPreset, false);
        Game_ShowCutsceneDialogue("mitra", "initial_overworld");
        d.mStep++;
        return false;
    case 1:
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(80));
        self.mV->mVisible = true;
        d.mStep++;
        [[fallthrough]];
    case 2:
        // a quarter into the screen and near the player
        if (t.mX - left < Fx(40) || p.mT->mX - t.mX > Fx(24))
        {
            return false;
        }
        t.mVelX = 0;
        t.mVelY = Fx(80);
        d.mStep++;
        [[fallthrough]];
    case 3:
        if (t.mY - p.mT->mY < Fx(24))
        {
            return false;
        }
        t.mVelX = Fx(80);
        t.mVelY = 0;
        self.mB->mImmovable = false;
        d.mStep++;
        [[fallthrough]];
    case 4:
        if (self.mB->mWasTouching == TOUCH_NONE)
        {
            return false;
        }
        t.mVelX = t.mVelY = 0;
        self.mB->mImmovable = true;
        Screen_Shake(FxF(0.02f), FxSec(0.3f));
        Game_PlaySfx("sun_guy_death_short");
        MitraOnBike(self, false);
        if (d.mBike)
        {
            d.mBike->mExists = true;
            d.mBike->mT->mX = t.mX - Fx(4);
            d.mBike->mT->mY = t.mY;
        }
        Screen_Flash(FxSec(1));
        d.mStep++;
        [[fallthrough]];
    case 5:
        if (Screen_FlashActive())
        {
            return false;
        }
        t.mVelX = -Fx(20);
        d.mStep++;
        [[fallthrough]];
    case 6:
        if (t.mX > p.mT->mX + Fx(3))
        {
            Ecs_FaceTowards(p, t.mX, t.mY);
            return false;
        }
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(20));
        d.mStep++;
        [[fallthrough]];
    case 7:
        if (t.mY > p.mT->mY + Fx(26))
        {
            Ecs_FaceTowards(p, t.mX, t.mY);
            return false;
        }
        t.mVelX = t.mVelY = 0;
        Game_ShowCutsceneDialogue("mitra", "initial_overworld");
        d.mState = MITRA_SECOND_WAIT;
        return true;
    default:
        return true;
    }
}

bool MitraExit(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case 0:
        Game_ShowCutsceneDialogue("mitra", "initial_overworld", 3);
        d.mStep++;
        return false;
    case 1:
        // back to the bike
        t.mVelX = Fx(20);
        t.mVelY = 0;
        d.mStep++;
        [[fallthrough]];
    case 2:
        if (d.mBike && FxLength(t.mX - d.mBike->mT->mX, t.mY - d.mBike->mT->mY) > Fx(8))
        {
            return false;
        }
        MitraOnBike(self, true);
        if (d.mBike)
        {
            d.mBike->mExists = false;
        }
        // off the screen
        t.mVelX = 0;
        t.mVelY = Fx(50);
        d.mStep++;
        [[fallthrough]];
    case 3:
        if (t.mY - Fx(gGame.mGridY * 160) < Fx(190))
        {
            return false;
        }
        d.mVolTarget = 0.2f;
        d.mVolReached = false;
        d.mStep++;
        [[fallthrough]];
    case 4:
        if (!d.mVolReached)
        {
            return false;
        }
        Game_PlaySong("overworld");
        self.mExists = false;
        return true;
    default:
        return true;
    }
}

// ---- FadeSwitchSong (Events/FadeSwitchSong.cs): fades the song out, switches to `next` and fades that in (its
// VolumeEvent(0, 1.6f), whose target it sets to 1 when it goes)
struct FadeSwitchSongData final
{
    const char* mNext;
    float mTarget;
    bool mReached;
    bool mSwitched;
};

void FadeSwitchSongUpdate(Entity& self)
{
    FadeSwitchSongData& d = self.Data<FadeSwitchSongData>();
    if (!d.mSwitched && d.mReached)
    {
        Game_PlaySong(d.mNext, 0);
        d.mTarget = 1;
        d.mReached = false;
        d.mSwitched = true;
    }
    if (!d.mReached)
    {
        fx volume = FxF(Game_SongVolume());
        d.mReached = MoveTo(volume, FxF(d.mTarget), FxF(1.6f));
        Game_SetSongVolume(volume.mV);
    }
    else
    {
        self.mExists = false;
    }
}

const EntityClass kFadeSwitchSong = {
    "FadeSwitchSong", FadeSwitchSongUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void SpawnFadeSwitchSong(const char* next)
{
    Entity* e = Ecs_Create(kFadeSwitchSong, 0, 0, PART_BRAIN);
    if (!e)
    {
        return;
    }
    Ecs_SetSolid(*e, false);
    Ecs_MarkSpawned(*e);
    FadeSwitchSongData& d = e->Data<FadeSwitchSongData>();
    d = FadeSwitchSongData();
    d.mNext = next;
}

// MitraCliff.Entrance: rides in from the right to the player, gets off and hands over the shoes.
bool MitraCliffEntrance(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    Transform& t = *self.mT;
    const Entity& p = *World_Player();
    switch (d.mStep)
    {
    case 0:
        SpawnFadeSwitchSong("mitra");
        Game_ShowCutsceneDialogueFor("misc", "any", "mitra", 0);
        d.mStep++;
        return false;
    case 1:
        t.mVelX = -Fx(37);
        d.mStep++;
        [[fallthrough]];
    case 2:
        if (t.mX > p.mT->mX)
        {
            return false;
        }
        t.mVelX = t.mVelY = 0;
        MitraOnBike(self, false);
        if (d.mBike)
        {
            d.mBike->mT->mX = t.mX - kFxOne;
            d.mBike->mT->mY = t.mY - kFxOne;
            d.mBike->mExists = true;
        }
        if (gGame.mTradeState == TRADE_SHOES)
        {
            Game_ShowCutsceneDialogueFor("misc", "any", "mitra", 1);
            gGame.mTradeState = TRADE_NONE;
        }
        else
        {
            Game_ShowCutsceneDialogueFor("misc", "any", "mitra", 2);
        }
        d.mStep++;
        return false;
    default:
        gGame.mCanJump = true;
        return true;
    }
}

// MitraCliff.Exit: back on the bike and off the screen to the right.
bool MitraCliffExit(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case 0:
        Game_ShowCutsceneDialogueFor("misc", "any", "mitra", 3);
        d.mStep++;
        return false;
    case 1:
        if (d.mBike)
        {
            d.mBike->mExists = false;
        }
        MitraOnBike(self, true);
        t.mVelX = Fx(37);
        Ecs_SetSolid(self, false); // (no pushing the player off the screen)
        d.mStep++;
        [[fallthrough]];
    case 2:
        // until it wraps round, off the screen (MapUtilities.GetInGridPosition)
        if (t.mX - Fx(FxFloor(t.mX / 160) * 160) > Fx(10))
        {
            return false;
        }
        Preset_SetAlive(*self.mPreset, false);
        self.mExists = false;
        SpawnFadeSwitchSong("cliff");
        return true;
    default:
        return true;
    }
}

void MitraCliffUpdate(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    const Entity& p = *World_Player();
    if (Player_State() == PS_AIR)
    {
        d.mPlayerJumped = true;
    }
    const fx gx = p.mT->mX - Fx(gGame.mGridX * 160);
    const fx gy = p.mT->mY - Fx(gGame.mGridY * 160);
    if (!self.mV->mVisible && gy > Fx(50))
    {
        self.mT->mX = Fx(gGame.mGridX * 160 + 180);
        self.mT->mY = Fx(gGame.mGridY * 160 + 80);
        self.mT->mLastX = self.mT->mX;
        self.mT->mLastY = self.mT->mY;
        self.mV->mVisible = true;
        d.mStep = 0;
        Game_StartCutscene(self, MitraCliffEntrance);
    }
    else if (!d.mExiting && self.mV->mVisible && (gx > Fx(135) || gy < Fx(49)))
    {
        d.mExiting = true;
        d.mStep = 0;
        Game_StartCutscene(self, MitraCliffExit);
    }
}

void MitraUpdate(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    if (d.mFields || d.mCliff)
    {
        Ecs_BaseUpdate(self);
        MitraFacePlayerPose(self);
        if (d.mCliff)
        {
            MitraCliffUpdate(self);
        }
        return;
    }
    const Entity& p = *World_Player();
    if (d.mState == MITRA_INITIAL_WAIT)
    {
        const fx dy = self.mT->mY - p.mT->mY;
        if ((dy < 0 ? -dy : dy) < Fx(48) && d.mVolReached)
        {
            d.mState = MITRA_ENTRANCE;
            d.mStep = 0;
            Game_StartCutscene(self, MitraEntrance);
        }
    }
    else if (d.mState == MITRA_SECOND_WAIT && DistanceToPlayer(self) > Fx(48))
    {
        d.mState = MITRA_EXIT;
        d.mStep = 0;
        Game_StartCutscene(self, MitraExit);
    }
    Ecs_BaseUpdate(self);
    MitraFacePlayerPose(self);
    // (VolumeEvent.Update, the sub-entity after it)
    if (!d.mVolReached)
    {
        fx volume = FxF(Game_SongVolume());
        d.mVolReached = MoveTo(volume, FxF(d.mVolTarget), FxF(0.4f));
        Game_SetSongVolume(volume.mV);
    }
}

// MitraFields.GetInteractionText (never empty there).
void MitraFieldsTalk(MitraData& d)
{
    if (d.mInitial)
    {
        Event_Inc("mitra.fieldinit");
        if (Dialog_IsSceneDirty("mitra", "init"))
        {
            Game_ShowDialogue("mitra", "init");
            return;
        }
        Game_ShowDialogue("mitra", "init", 1 - Event_Get("mitra.wares"));
        Dialog_SetSceneProgress("mitra", "init", 2);
        return;
    }
    if (gGame.mTradeState == TRADE_SHOES)
    {
        gGame.mTradeState = TRADE_NONE;
        gGame.mCanJump = true;
        Game_ShowDialogueFor("misc", "any", "mitra", 1);
        return;
    }
    static const char* const kBosses[] = { "REDCAVE", "BEDROOM", "CROWD", "CIRCUS", "APARTMENT", "HOTEL" };
    bool allBossesDead = true;
    for (const char* b : kBosses)
    {
        allBossesDead = allBossesDead && Game_BossDefeated(b);
    }
    const int cards = Inventory_CardCount();
    if (cards < 36 && allBossesDead)
    {
        if (!d.mGivenQuestHint && Event_Get("GoQuestProgress") == 0)
        {
            d.mGivenQuestHint = true;
            Game_ShowDialogue("mitra", "quest_event");
            return;
        }
        if (cards == 0)
        {
            Game_ShowDialogue("mitra", "card_hints", 36);
            return;
        }
        int missing[36];
        int count = 0;
        for (int i = 0; i < 36; i++)
        {
            if (!gGame.mCardStatus[i])
            {
                missing[count++] = i;
            }
        }
        Game_ShowDialogue("mitra", "card_hints", missing[Rng_Range(0, count)]);
        return;
    }
    if (!d.mGivenInitialHint)
    {
        d.mGivenInitialHint = true;
        int hint = 0;
        if (Event_Get("WindmillOpened") == 0)
        {
            const bool redcave = Game_BossDefeated("REDCAVE");
            const bool crowd = Game_BossDefeated("CROWD");
            if (!redcave && !crowd)
            {
                hint = Rng_Range(1, 3);
            }
            else if (redcave && crowd)
            {
                hint = 3;
            }
            else if (redcave)
            {
                hint = 8;
            }
            else
            {
                hint = 7;
            }
        }
        else if (!allBossesDead)
        {
            hint = 4;
        }
        else if (!gGame.mHasBroom[BROOM_TRANSFORMER])
        {
            hint = 5;
        }
        else
        {
            hint = 6;
        }
        Game_ShowDialogue("mitra", "game_hints", hint);
        return;
    }
    if (!d.mGivenQuestHint && Event_Get("GoQuestProgress") == 0)
    {
        d.mGivenQuestHint = true;
        Game_ShowDialogue("mitra", "quest_event");
        return;
    }
    Game_ShowDialogue("mitra", "general_banter");
}

bool MitraInteract(Entity& self, Facing)
{
    MitraData& d = self.Data<MitraData>();
    if (d.mFields)
    {
        MitraFieldsTalk(d);
        return true;
    }
    if (d.mCliff)
    {
        Game_ShowDialogueFor("misc", "any", "mitra", d.mPlayerJumped ? 5 : 4);
        return true;
    }
    if (Event_Get("mitra.wares") == 0)
    {
        Event_Inc("mitra.wares");
        Game_ShowCutsceneDialogue("mitra", "initial_overworld");
        return true;
    }
    if (d.mState != MITRA_EXIT)
    {
        d.mState = MITRA_EXIT;
        d.mStep = 0;
        Game_StartCutscene(self, MitraExit);
    }
    return false;
}

void MitraCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

void MitraDestroy(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    if (d.mBike)
    {
        Ecs_Free(*d.mBike);
        d.mBike = nullptr;
    }
}

const EntityClass kMitra = {
    "Mitra",      MitraUpdate, nullptr, MitraCollided, MitraInteract, nullptr, nullptr, MitraDestroy,
    0,            GROUP_PLAYER, ECF_MAP_COLLISION | ECF_INTERACTABLE,
};

void BikeCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kBike = {
    "Bike", nullptr, nullptr, BikeCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};
// ---- MiaoXiao + MiaoXiaoFollower (RunningTradeNPCs/MiaoXiao.cs) ----------------------------------------------------
// The cat asks to shadow the player, then follows them (as Player.follower) round the 4x3 screens about its own one,
// chipping in on some of them.
struct MiaoScreen
{
    int mX;
    int mY;
    const char* mName;
    const char* mEventCheck;
    int mDiagId;
};

const MiaoScreen kMiaoScreens[] = {
    { 3, 2, "shopkeep", "shopkeep.init", 0 },
    { 1, 2, "mitra", "mitra.fieldinit", 1 },
    { 2, 1, "nexus", nullptr, 2 },
};

constexpr float kMiaoScreenTimeout = 1.8f;
constexpr float kMiaoHintTimeout = 70.0f;

struct MiaoData final
{
    Entity* mFollower; // MiaoXiao: its follower; the follower: its dust
    float mTimer;
    int mDiag;              // kMiaoScreens index of currentDiag, -1 for none
    const char* mWaitEvent; // wait_on_event
    bool mOnConveyor;
    int mBoundX;            // boundaries: 4x3 screens from (mBoundX, mBoundY)
    int mBoundY;
};

Entity* gMiaoFollower = nullptr; // Player.follower

int MiaoScreenAt(const MiaoData& d)
{
    const int x = gGame.mGridX - d.mBoundX;
    const int y = gGame.mGridY - d.mBoundY;
    for (int i = 0; i < static_cast<int>(sizeof(kMiaoScreens) / sizeof(kMiaoScreens[0])); i++)
    {
        if (kMiaoScreens[i].mX == x && kMiaoScreens[i].mY == y)
        {
            return i;
        }
    }
    return -1;
}

bool MiaoInteract(Entity& self, Facing)
{
    if (gGame.mSpookedMonster && !Dialog_IsSceneFinished("miao", "philosophy"))
    {
        Game_ShowDialogue("miao", "philosophy");
    }
    else
    {
        // Ask to shadow, then swap places
        Game_ShowDialogue("miao", "init");
        self.mExists = false;
        Entity* f = self.Data<MiaoData>().mFollower;
        if (f)
        {
            f->mExists = true;
            f->mPlayerOwned = true;
            gMiaoFollower = f;
            Entity* dust = f->Data<MiaoData>().mFollower;
            if (dust)
            {
                dust->mExists = true;
                dust->mPlayerOwned = true;
            }
        }
    }
    return true;
}

void MiaoCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

void MiaoFollowerUpdate(Entity& self)
{
    MiaoData& d = self.Data<MiaoData>();
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    Transform& t = *self.mT;
    float ox = (Ecs_CenterX(p) - Ecs_CenterX(self)).mV;
    float oy = (Ecs_CenterY(p) - Ecs_CenterY(self)).mV;
    if (ox * ox + oy * oy > 16 * 16)
    {
        // Vector2.Normalize
        const float inv = 1.0f / sqrtf(ox * ox + oy * oy);
        ox *= inv;
        oy *= inv;
        t.mVelX = fx(ox * 70);
        t.mVelY = fx(oy * 70);
    }
    else
    {
        t.mVelX = t.mVelY = 0;
    }
    Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    Ecs_PlayFacing(self, "walk", "@2"); // (MiaoGraphics' anims)

    if (Entity* dust = d.mFollower)
    {
        if (!d.mOnConveyor && Ecs_AnimIs(*dust, "unpoof"))
        {
            Ecs_Play(*dust, "poof");
        }
        dust->mT->mX = t.mX - Fx(2);
        dust->mT->mY = t.mY;
    }
    d.mOnConveyor = false;

    if (d.mWaitEvent && Event_Get(d.mWaitEvent) != 0)
    {
        d.mDiag = MiaoScreenAt(d);
        d.mTimer = kMiaoScreenTimeout;
        d.mWaitEvent = nullptr;
    }

    d.mTimer -= kDeltaTime;
    if (d.mTimer < 0 && !World_InTransition() && Game_DialogueFinished())
    {
        int id = d.mDiag >= 0 ? kMiaoScreens[d.mDiag].mDiagId : -1;
        if (d.mDiag >= 0)
        {
            char name[32];
            snprintf(name, sizeof(name), "miao.%s", kMiaoScreens[d.mDiag].mName);
            Event_Inc(name);
        }
        else if (!gGame.mSpookedMonster)
        {
            id = 3; // Ask where Icky is
        }
        else
        {
            bool all = true;
            for (const MiaoScreen& s : kMiaoScreens)
            {
                char name[32];
                snprintf(name, sizeof(name), "miao.%s", s.mName);
                all = all && Event_Get(name) != 0;
            }
            if (all)
            {
                id = Rng_Range(5, 8); // Random talk when all other dialogues have happened
            }
        }
        if (id != -1)
        {
            Game_ShowDialogue("miao", "randoms", id);
        }
        d.mTimer = kMiaoHintTimeout;
        d.mDiag = -1;
    }
}

void MiaoFollowerEvent(Entity& self, int gameEvent)
{
    if (!self.mExists)
    {
        return;
    }
    MiaoData& d = self.Data<MiaoData>();
    const bool inBounds = gGame.mGridX >= d.mBoundX && gGame.mGridX < d.mBoundX + 4 && gGame.mGridY >= d.mBoundY &&
                          gGame.mGridY < d.mBoundY + 3;
    if (gameEvent == GEV_START_WARP || (gameEvent == GEV_END_SCREEN_TRANSITION && !inBounds))
    {
        Game_ShowDialogue("miao", "randoms", 4);
        self.mExists = false;
        self.mPlayerOwned = false;
        if (d.mFollower)
        {
            d.mFollower->mExists = false;
            d.mFollower->mPlayerOwned = false;
        }
        gMiaoFollower = nullptr;
    }
    else if (gameEvent == GEV_END_SCREEN_TRANSITION)
    {
        const int i = MiaoScreenAt(d);
        if (i >= 0)
        {
            const MiaoScreen& s = kMiaoScreens[i];
            if (Event_Get(s.mEventCheck) == 0)
            {
                d.mWaitEvent = s.mEventCheck;
            }
            char name[32];
            snprintf(name, sizeof(name), "miao.%s", s.mName);
            if ((!s.mEventCheck || Event_Get(s.mEventCheck) != 0) && Event_Get(name) == 0)
            {
                d.mTimer = kMiaoScreenTimeout;
                d.mDiag = i;
            }
        }
    }
    else if (gameEvent == GEV_START_SCREEN_TRANSITION && d.mDiag >= 0)
    {
        d.mTimer = kMiaoHintTimeout;
        d.mDiag = -1;
    }
}

void MiaoFollowerMapEvent(Entity& self, MapEvent ev, uint8_t, fx, fx)
{
    if (ev != MAPEV_CONVEYOR)
    {
        return;
    }
    MiaoData& d = self.Data<MiaoData>();
    if (Entity* dust = d.mFollower)
    {
        if (!Ecs_AnimIs(*dust, "unpoof"))
        {
            Ecs_Play(*dust, "unpoof");
            dust->mV->mVisible = true;
        }
    }
    d.mOnConveyor = true;
}

void MiaoFollowerDestroy(Entity& self)
{
    if (gMiaoFollower == &self)
    {
        gMiaoFollower = nullptr;
    }
}

const EntityClass kMiaoXiao = {
    "MiaoXiao", nullptr, nullptr, MiaoCollided, MiaoInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER, ECF_INTERACTABLE,
};

const EntityClass kMiaoFollower = {
    "MiaoXiaoFollower", MiaoFollowerUpdate, nullptr, nullptr, nullptr, MiaoFollowerMapEvent, MiaoFollowerEvent,
    MiaoFollowerDestroy, 0, 0, ECF_MAP_COLLISION | ECF_PROBE_OWNED,
};

const EntityClass kMiaoDust = {
    "MiaoXiaoDust", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* MakeMiao(const EntityClass& cls, fx x, fx y, const MapEntity* preset)
{
    Entity* e = Ecs_Create(cls, x, y, PART_VISUAL | PART_BRAIN, preset);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetSheet(*e, "fields_npcs", 16, 16);
    Ecs_Play(*e, "walk_d@2");
    e->mB->mW = e->mB->mH = 12;
    e->mV->mOffX = e->mV->mOffY = Fx(2);
    return e;
}

// ---- OutsideMonster, Box, BoxIcky (RunningTradeNPCs/OutsideMonster.cs) --------------------------------------------
// The monster guards Icky's box; with the cat along it is spooked off, the box opens and Icky walks out.
struct TradeData final
{
    uint8_t mStep;      // within its cutscene
    bool mLastBit;      // BoxIcky._state: MoveLastBit running
    bool mLastBitDone;
    Entity* mSub;       // the monster's box, the box's Icky
    const MapEntity* mPreset; // the box: the monster's preset
};

// MapUtilities.GetInGridPosition
float InGrid(fx v)
{
    return v.mV - static_cast<float>(static_cast<int>(v.mV / 160) * 160);
}

void TradeCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

bool OutsideMonsterCutscene(Entity& self)
{
    TradeData& d = self.Data<TradeData>();
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case 0:
        Game_ShowCutsceneDialogue("goldman", "etc", 0);
        d.mStep = 1;
        return false;
    case 1:
        Ecs_SetSolid(self, false);
        t.mVelX = 0;
        t.mVelY = -Fx(40);
        Ecs_Play(self, "walk_u@3");
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        if (InGrid(t.mY) > 6 * 16)
        {
            return false;
        }
        t.mVelX = Fx(40);
        t.mVelY = 0;
        Ecs_Play(self, "walk_r@3");
        d.mStep = 3;
        [[fallthrough]];
    default:
        if (InGrid(t.mX) > 10) // wraps around when on next screen
        {
            return false;
        }
        self.mExists = false;
        return true;
    }
}

bool OutsideMonsterInteract(Entity& self, Facing)
{
    if (gMiaoFollower)
    {
        gGame.mSpookedMonster = true;
        self.Data<TradeData>().mStep = 0;
        Game_StartCutscene(self, OutsideMonsterCutscene);
    }
    else
    {
        Game_ShowDialogue("goldman", "outside");
    }
    return true;
}

bool BoxIckyCutscene(Entity& self)
{
    TradeData& d = self.Data<TradeData>();
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case 0:
        Event_Inc("icky.rescued");
        t.mVelX = Fx(20);
        t.mVelY = 0;
        Ecs_Play(self, "walk_r");
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (InGrid(t.mX) < 85)
        {
            return false;
        }
        t.mVelX = t.mVelY = 0;
        Ecs_Play(self, "walk_d");
        Game_ShowCutsceneDialogue("goldman", "etc", 3);
        d.mStep = 2;
        return false;
    default:
        d.mLastBit = true;
        return true;
    }
}

bool BoxCutscene(Entity& self)
{
    TradeData& d = self.Data<TradeData>();
    if (d.mStep == 0)
    {
        Game_ShowCutsceneDialogue("goldman", "etc", 2);
        d.mStep = 1;
        return false;
    }
    gGame.mTradeState = TRADE_BOX;
    self.mExists = false;
    if (d.mPreset)
    {
        Preset_SetAlive(*d.mPreset, false);
    }
    if (Entity* icky = d.mSub)
    {
        icky->mExists = true;
        icky->Data<TradeData>().mStep = 0;
        Game_StartCutscene(*icky, BoxIckyCutscene);
    }
    return true;
}

bool BoxInteract(Entity& self, Facing)
{
    if (!gGame.mSpookedMonster)
    {
        return false;
    }
    Ecs_Play(self, "open");
    Game_PlaySfx("broom_hit");
    self.Data<TradeData>().mStep = 0;
    Game_StartCutscene(self, BoxCutscene);
    return true;
}

bool BoxIckyInteract(Entity&, Facing)
{
    Game_ShowDialogue("goldman", "etc", 4);
    return true;
}

void BoxIckyUpdate(Entity& self)
{
    TradeData& d = self.Data<TradeData>();
    Ecs_BaseUpdate(self);
    // MoveLastBit
    if (d.mLastBit && !d.mLastBitDone)
    {
        Transform& t = *self.mT;
        if (t.mVelX == 0)
        {
            t.mVelX = Fx(20);
            Ecs_Play(self, "walk_r");
        }
        if (InGrid(t.mX) >= 6 * 16)
        {
            t.mVelX = 0;
            Ecs_Play(self, "walk_d");
            d.mLastBitDone = true;
        }
    }
}

const EntityClass kOutsideMonster = {
    "OutsideMonster", nullptr, nullptr, TradeCollided, OutsideMonsterInteract, nullptr, nullptr, nullptr,
    0,                GROUP_PLAYER, ECF_INTERACTABLE,
};

const EntityClass kBox = {
    "Box", nullptr, nullptr, TradeCollided, BoxInteract, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, ECF_INTERACTABLE,
};

const EntityClass kBoxIcky = {
    "BoxIcky", BoxIckyUpdate, nullptr, TradeCollided, BoxIckyInteract, nullptr, nullptr, nullptr,
    0,         GROUP_PLAYER,  ECF_INTERACTABLE,
};

// InsideMonster: in the house, wants the 12 dusts down the holes; then steps aside from the chest.
struct InsideMonsterData final
{
    int mDustLeft;
};

void InsideMonsterUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    if (Preset_Activated(*self.mPreset) && t.mVelX != 0 && t.mX.mV >= static_cast<float>(self.mPreset->mX) + 31.5f)
    {
        t.mVelX = 0;
        Ecs_Play(self, "walk_d@3");
    }
}

void InsideMonsterEvent(Entity& self, int gameEvent)
{
    if (!self.mExists)
    {
        return;
    }
    if (gameEvent == GEV_DUST_FALL)
    {
        if (--self.Data<InsideMonsterData>().mDustLeft == 0)
        {
            Game_ShowDialogue("goldman", "etc", 1);
            Preset_SetActivated(*self.mPreset, true);
            self.mT->mVelX = Fx(20);
            Ecs_Play(self, "walk_r@3");
        }
    }
    else if (gameEvent == GEV_EMPTY_TREASURE)
    {
        Game_ShowDialogueFor("misc", "any", "treasure", 8); // shopkeep must have stolen it!
    }
}

bool InsideMonsterInteract(Entity& self, Facing)
{
    if (Preset_Activated(*self.mPreset))
    {
        Game_ShowDialogue("goldman", "etc", 1);
    }
    else
    {
        Game_ShowDialogue("goldman", "inside");
    }
    return true;
}

const EntityClass kInsideMonster = {
    "InsideMonster", InsideMonsterUpdate, nullptr, TradeCollided, InsideMonsterInteract, nullptr, InsideMonsterEvent,
    nullptr,         0,                   GROUP_PLAYER,  ECF_INTERACTABLE | ECF_MAP_COLLISION,
};

Entity* MakeTradeNpc(const EntityClass& cls, fx x, fx y, const char* anim, const MapEntity* preset)
{
    Entity* e = Ecs_Create(cls, x, y, PART_VISUAL | PART_BRAIN, preset);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetSheet(*e, "fields_npcs", 16, 16);
    Ecs_Play(*e, anim);
    e->mB->mW = e->mB->mH = 16;
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, true);
    return e;
}

// ---- ShopKeep, ShopItem (RunningTradeNPCs/ShopKeep.cs) --------------------------------------------------------------
// The beaver in the fields shop: trades Icky's box for the jump shoes (preset Activated once given the box).
struct ShopKeepData final
{
    Entity* mItems[3];
};

struct ShopItemData final
{
    bool mChecked;
    fx mStartX;
    fx mStartY;
};

// GetDialogue("misc", "any", "tradenpc", i)
const char* TradeNpcText(int i)
{
    const char* text = Dialog_GetFor("misc", "any", "tradenpc", i);
    return text ? text : "";
}

// GlobalState.Dialogue = the lines joined with spaces.
void ShowTradeNpcText(int a, int b, int c = -1)
{
    char text[512];
    if (c < 0)
    {
        snprintf(text, sizeof(text), "%s %s", TradeNpcText(a), TradeNpcText(b));
    }
    else
    {
        snprintf(text, sizeof(text), "%s %s %s", TradeNpcText(a), TradeNpcText(b), TradeNpcText(c));
    }
    Game_ShowText(text);
}

bool ShopKeepInteract(Entity& self, Facing)
{
    const MapEntity& preset = *self.mPreset;
    if (Event_Get("shopkeep.init") == 0)
    {
        Game_ShowDialogueFor("misc", "any", "tradenpc", 0);
        Event_Inc("shopkeep.init");
    }
    else if (Preset_Activated(preset))
    {
        Game_ShowDialogueFor("misc", "any", "tradenpc", 1);
    }
    else if (gGame.mTradeState == TRADE_BOX)
    {
        Preset_SetActivated(preset, true);
        if (gGame.mCanJump)
        {
            // Young can already jump, so shopkeep exchanges something else for the box
            gGame.mTradeState = TRADE_NONE;
            if (gGame.mCardStatus[43])
            {
                ShowTradeNpcText(2, 11, 3);
                gGame.mCurHealth = gGame.mMaxHealth;
            }
            else
            {
                ShowTradeNpcText(2, 11);
                Treasure_GiveCard(self.mT->mX + Fx(20), self.mT->mY, 43);
            }
        }
        else
        {
            ShowTradeNpcText(2, 4);
            gGame.mTradeState = TRADE_SHOES;
            if (Entity* shoes = self.Data<ShopKeepData>().mItems[2])
            {
                shoes->mT->mAccY = -Fx(10); // ActivateAnim
            }
        }
    }
    else
    {
        Game_ShowDialogueFor("misc", "any", "tradenpc", 5);
    }
    return true;
}

void ShopItemUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    if (t.mVelY < -Fx(10) && t.mVelY > -Fx(15))
    {
        Ecs_Flicker(self, FxSec(1.0f));
    }
    else if (t.mVelY < -Fx(25))
    {
        const ShopItemData& d = self.Data<ShopItemData>();
        t.mAccX = t.mAccY = 0;
        t.mVelX = t.mVelY = 0;
        t.mX = d.mStartX;
        t.mY = d.mStartY;
        Ecs_SetFrame(self, Ecs_Frame(self) + 1);
        self.mV->mOpacity = 0;
    }
    MoveTo(self.mV->mOpacity, kFxOne, FxF(0.3f));
}

bool ShopItemInteract(Entity& self, Facing)
{
    ShopItemData& d = self.Data<ShopItemData>();
    if (d.mChecked)
    {
        Game_ShowDialogueFor("misc", "any", "tradenpc", 6);
    }
    else
    {
        Game_ShowDialogueFor("misc", "any", "tradenpc", Ecs_Frame(self) - 47);
        d.mChecked = true;
    }
    return true;
}

const EntityClass kShopKeep = {
    "ShopKeep", nullptr, nullptr, TradeCollided, ShopKeepInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER, ECF_INTERACTABLE,
};

const EntityClass kShopItem = {
    "ShopItem", ShopItemUpdate, nullptr, TradeCollided, ShopItemInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER,   ECF_INTERACTABLE,
};
} // namespace

void Miao_MoveFollowerLast()
{
    if (gMiaoFollower)
    {
        Ecs_MoveLast(*gMiaoFollower);
        if (Entity* dust = gMiaoFollower->Data<MiaoData>().mFollower)
        {
            Ecs_MoveLast(*dust);
        }
    }
}

Entity* Spawn_OutsideMonster(const MapEntity& preset, Entity&)
{
    Entity* e = MakeTradeNpc(kOutsideMonster, Fx(preset.mX), Fx(preset.mY), "walk_d@3", &preset);
    if (!e)
    {
        return nullptr;
    }
    Entity* box = MakeTradeNpc(kBox, e->mT->mX, e->mT->mY - Fx(20), "closed", nullptr);
    if (box)
    {
        box->Data<TradeData>().mPreset = &preset;
        Entity* icky = MakeTradeNpc(kBoxIcky, box->mT->mX, box->mT->mY - Fx(10), "walk_r", nullptr);
        if (icky)
        {
            Ecs_KeepPooled(*icky);
            box->Data<TradeData>().mSub = icky;
        }
        e->Data<TradeData>().mSub = box;
    }
    if (gGame.mSpookedMonster)
    {
        e->mExists = false;
    }
    return e;
}

Entity* Spawn_ShopKeep(const MapEntity& preset, Entity&)
{
    Entity* e = MakeTradeNpc(kShopKeep, Fx(preset.mX), Fx(preset.mY), "a", &preset);
    if (!e)
    {
        return nullptr;
    }
    static const int kItemFrames[3] = { 54, 55, 56 };
    for (int i = 0; i < 3; ++i)
    {
        int frame = kItemFrames[i];
        if (i == 2 && (Preset_Activated(preset) || gGame.mCanJump))
        {
            frame = 57;
        }
        const fx x = e->mT->mX - Fx(30) + Fx(34 * i);
        const fx y = e->mT->mY + Fx(32);
        Entity* item = Ecs_Create(kShopItem, x, y, PART_VISUAL | PART_BRAIN);
        if (item)
        {
            Ecs_SetSheet(*item, "fields_npcs", 16, 16);
            Ecs_SetFrame(*item, frame);
            item->mV->mLayer = LAYER_BG_ENTITIES;
            item->mB->mW = item->mB->mH = 16;
            item->mB->mImmovable = true;
            Ecs_SetSolid(*item, true);
            ShopItemData& d = item->Data<ShopItemData>();
            d.mStartX = x;
            d.mStartY = y;
        }
        e->Data<ShopKeepData>().mItems[i] = item;
    }
    return e;
}

Entity* Spawn_InsideMonster(const MapEntity& preset, Entity&)
{
    Entity* e = MakeTradeNpc(kInsideMonster, Fx(preset.mX), Fx(preset.mY), "walk_d@3", &preset);
    if (!e)
    {
        return nullptr;
    }
    e->Data<InsideMonsterData>().mDustLeft = 12;
    if (!gGame.mSpookedMonster)
    {
        e->mExists = false;
    }
    else if (Preset_Activated(preset))
    {
        e->mT->mX += Fx(32);
    }
    else
    {
        const fx left = Fx(preset.mX / 160 * 160);
        const fx top = Fx(preset.mY / 160 * 160);
        for (int i = 0; i < 3; i++)
        {
            Spawn_DustAt(left + Fx(16), top + Fx(16 + 32 * i));
        }
    }
    return e;
}

Entity* Spawn_MiaoXiao(const MapEntity& preset, Entity&)
{
    Entity* e = MakeMiao(kMiaoXiao, Fx(preset.mX), Fx(preset.mY), &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, true);
    Entity* f = MakeMiao(kMiaoFollower, e->mT->mX, e->mT->mY, nullptr);
    if (f)
    {
        MiaoData& fd = f->Data<MiaoData>();
        fd.mTimer = kMiaoHintTimeout;
        fd.mDiag = -1;
        fd.mBoundX = preset.mX / 160; // (CURRENT_GRID_X/Y: the screen it is made on)
        fd.mBoundY = preset.mY / 160 - 1;
        Ecs_KeepPooled(*f);
        Entity* dust = Ecs_Create(kMiaoDust, 0, 0, PART_VISUAL);
        if (dust)
        {
            Ecs_SetSolid(*dust, false);
            Ecs_SetSheet(*dust, "dust");
            dust->mV->mLayer = LAYER_BG_ENTITIES;
            dust->mV->mVisible = false;
            Ecs_Play(*dust, "poofed");
            Ecs_KeepPooled(*dust);
            fd.mFollower = dust;
        }
        e->Data<MiaoData>().mFollower = f;
    }
    if (gMiaoFollower)
    {
        e->mExists = false;
    }
    return e;
}

Entity* Spawn_SageNexus(const MapEntity& preset, Entity&)
{
    if (Event_Get("SageDied") > 0)
    {
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    return SpawnSage(preset, SAGE_NEXUS, 64, 20, "enter_nexus");
}

Entity* Spawn_SageTerminal(const MapEntity& preset, Entity&)
{
    if (Event_Get("SageDied") > 0)
    {
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    Entity* e = Spawn_Basic(kSageTerminal, preset, "sage");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 10;
    e->mV->mOffX = e->mV->mOffY = Fx(3);
    e->mT->mX += Fx(3);
    e->mT->mY += Fx(3);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mB->mImmovable = true;
    Ecs_Play(*e, "idle_d");
    return e;
}

Entity* Spawn_SageOverworld(const MapEntity& preset, Entity&)
{
    if (Game_BossDefeated("BEDROOM"))
    {
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    return SpawnSage(preset, SAGE_OVERWORLD, 56, 28, "bedroom_entrance");
}

Entity* Spawn_SageBedroom(const MapEntity& preset, Entity&)
{
    if (Game_LeftAfterBoss(gGame.mMapName))
    {
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    return SpawnSage(preset, SAGE_BEDROOM, 48, 24, "after_boss");
}

// DungeonSage: gone once the player has left the dungeon after its boss.
Entity* SpawnDungeonSage(const MapEntity& preset, uint8_t kind, int init, int stop, const char* scene)
{
    if (Game_LeftAfterBoss(gGame.mMapName))
    {
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    return SpawnSage(preset, kind, init, stop, scene);
}

Entity* Spawn_SageRedCave(const MapEntity& preset, Entity&)
{
    return SpawnDungeonSage(preset, SAGE_REDCAVE, 28, 16, "one");
}

Entity* Spawn_SageCrowd(const MapEntity& preset, Entity&)
{
    return SpawnDungeonSage(preset, SAGE_CROWD, 46, 24, "one");
}

Entity* Spawn_Rock(const MapEntity& preset, Entity&)
{
    // Rock.GetSprite: SPACE's rocks are on its NPC sheet, the CELL's a frame on
    const bool space = gGame.mMapName && strcmp(gGame.mMapName, "SPACE") == 0;
    Entity* e = Spawn_Basic(kRock, preset, space ? "space_npcs" : "note_rock");
    if (e)
    {
        e->mB->mImmovable = true;
        Ecs_SetFrame(*e, space ? (preset.mX / 160 > 5 ? 31 : 30) : (Game_IsCell() ? 1 : 0));
    }
    return e;
}

Entity* Spawn_Statue(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kStatue, preset, "sage_statue");
    if (e)
    {
        e->mB->mImmovable = true;
        Ecs_SetFrame(*e, 0);
    }
    return e;
}

Entity* Spawn_Sadbro(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSadbro, preset, "sadman");
    if (e)
    {
        e->mB->mImmovable = true;
        Ecs_Play(*e, "a");
    }
    return e;
}

Entity* Spawn_DungeonStatue(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kDungeonStatue, preset, "big_statue", 32, 48);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    e->mB->mW = 30;
    e->mB->mH = 16;
    e->mV->mOffX = Fx(1);
    e->mV->mOffY = Fx(32);
    e->mT->mX += Fx(1);
    e->mT->mY += Fx(32);
    if (Event_Get("WindmillOpened") != 0)
    {
        // DungeonStatue.MoveDir: up for frame 0, else right.
        if (preset.mFrame == 0)
        {
            e->mT->mY -= Fx(32);
        }
        else
        {
            e->mT->mX += Fx(32);
        }
    }
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    Ecs_SetFrame(*e, preset.mFrame);
    return e;
}

Entity* Spawn_EyeLight(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kEyeLight, preset, "eyelight", 16, 16, PART_VISUAL);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetSolid(*e, false);
    Ecs_Play(*e, "glow");
    const bool bright = strcmp(Preset_TypeValue(preset), "bright") == 0;
    Entity* light = Ecs_Create(bright ? kBrightGlowLight : kGlowLight, e->mT->mX + Fx(8), e->mT->mY + Fx(6), PART_VISUAL,
                                  nullptr, e);
    if (light)
    {
        Darkness_MakeLight(*light, bright ? "5-frame-glow-bright" : "5-frame-glow");
        Ecs_Play(*light, "glow");
    }
    return e;
}

Entity* Spawn_BedroomBounceLight(const MapEntity& preset, Entity& player)
{
    if (Game_BossDefeated("BEDROOM") && strcmp(gGame.mMapName, "BEDROOM") == 0)
    {
        // (the C#'s constructor picks its direction before it finds the boss dead)
        Rng_Range(0, 4);
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    Entity* e = Ecs_Create(kBounceLight, player.mT->mX, player.mT->mY, PART_VISUAL | PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    Darkness_MakeLight(*e, "glow-light");
    e->mV->mScale = Fx(2);
    e->mB->mW = e->mB->mH = 32;
    Darkness_SetPlayerLight(*e);
    const int velDecider = Rng_Range(0, 4);
    e->mT->mVelX = velDecider % 2 == 0 ? -Fx(30) : Fx(30);
    e->mT->mVelY = velDecider / 2 == 0 ? -Fx(30) : Fx(30);
    e->Data<BounceLightData>().mLockOnTimer = FxSec(2.0f);
    return e;
}

Entity* Spawn_FollowerBro(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kFollowerBro, preset, "follower_bro", 16, 24);
    if (e)
    {
        e->mV->mOpacity = 0;
        Ecs_Play(*e, "wait");
    }
    return e;
}

Entity* Spawn_HealthCicada(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kCicada, Fx(preset.mX), Fx(preset.mY), PART_VISUAL | PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 16;
    Ecs_SetSolid(*e, false);
    e->mV->mVisible = false;
    CicadaData& d = e->Data<CicadaData>();
    d.mGameCoords = true;
    Entity* c = Ecs_Create(kCicadaFly, -Fx(16), Fx(30), PART_VISUAL, nullptr, e);
    if (!c)
    {
        return nullptr;
    }
    Ecs_SetSolid(*c, false);
    Ecs_SetSheet(*c, "life_cicada");
    Ecs_SetFrameBase(*c, preset.mFrame);
    c->mV->mLayer = LAYER_FG_SPRITES;
    c->mV->mVisible = false;
    d.mCicada = c;
    for (Entity*& box : d.mBoxes)
    {
        box = Ecs_Create(kCicadaBox, 0, 0, PART_VISUAL, nullptr, e);
        if (!box)
        {
            return nullptr;
        }
        Ecs_SetSolid(*box, false);
        Ecs_SetSheet(*box, "life_cicada_particle");
        box->mV->mLayer = LAYER_UI;
        Ecs_KeepPooled(*box);
    }
    CicadaPlaceDrawing(*e);
    return e;
}

Entity* Spawn_BigKey(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBigKey, preset, "key_green");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    e->mB->mW = 9;
    e->mV->mOffX = Fx(4);
    e->mT->mX += Fx(4);
    e->mT->mLastX = e->mT->mX;
    Ecs_SetFrame(*e, preset.mFrame);
    BigKeyData& d = e->Data<BigKeyData>();
    d.mSparkleMax = FxSec(0.8f);
    for (Entity*& s : d.mSparkles)
    {
        s = Ecs_Create(kSparkle, 0, 0, PART_VISUAL, nullptr, e);
        if (!s)
        {
            return nullptr;
        }
        Ecs_SetSolid(*s, false);
        Ecs_SetSheet(*s, "key_sparkle");
        s->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_KeepPooled(*s);
    }
    return e;
}

Entity* Spawn_MitraOverworld(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kMitra, preset, "mitra");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    e->mV->mVisible = false;
    MitraData& d = e->Data<MitraData>();
    d.mFacePlayer = true;
    d.mVolTarget = 0.3f;
    MitraOnBike(*e, true);
    d.mBike = Ecs_Create(kBike, e->mT->mX, e->mT->mY, PART_VISUAL, &preset, e);
    if (d.mBike)
    {
        Entity& b = *d.mBike;
        Ecs_SetSheet(b, "bike", 20, 20);
        b.mB->mW = 16;
        b.mB->mH = 10;
        b.mV->mOffX = Fx(2);
        b.mV->mOffY = Fx(11);
        b.mB->mImmovable = true;
        Ecs_Play(b, "r");
        b.mExists = false;
    }
    Ecs_PlayFacing(*e, "idle");
    return e;
}

Entity* Spawn_MitraCliff(const MapEntity& preset, Entity&)
{
    if (gGame.mCanJump)
    {
        Preset_SetAlive(preset, false);
        return nullptr;
    }
    Entity* e = Spawn_Basic(kMitra, preset, "mitra");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    e->mV->mVisible = false;
    MitraData& d = e->Data<MitraData>();
    d.mCliff = true;
    d.mFacePlayer = true;
    d.mVolReached = true;
    MitraOnBike(*e, true);
    d.mBike = Ecs_Create(kBike, e->mT->mX, e->mT->mY, PART_VISUAL, &preset, e);
    if (d.mBike)
    {
        Entity& b = *d.mBike;
        Ecs_SetSheet(b, "bike", 20, 20);
        b.mB->mW = 16;
        b.mB->mH = 10;
        b.mV->mOffX = Fx(2);
        b.mV->mOffY = Fx(11);
        b.mB->mImmovable = true;
        Ecs_Play(b, "r");
        b.mExists = false;
    }
    Ecs_PlayFacing(*e, "idle");
    return e;
}

Entity* Spawn_MitraFields(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kMitra, preset, "mitra");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    MitraData& d = e->Data<MitraData>();
    d.mFields = true;
    d.mFacePlayer = true;
    d.mVolReached = true;
    d.mInitial = !Dialog_IsSceneDirty("mitra", "init");
    MitraOnBike(*e, false);
    d.mBike = Ecs_Create(kBike, e->mT->mX - Fx(20), e->mT->mY, PART_VISUAL, &preset, e);
    if (d.mBike)
    {
        Entity& b = *d.mBike;
        Ecs_SetSheet(b, "bike", 20, 20);
        b.mB->mW = 16;
        b.mB->mH = 10;
        b.mV->mOffX = Fx(2);
        b.mV->mOffY = Fx(11);
        b.mB->mImmovable = true;
        Ecs_Play(b, "r");
    }
    Ecs_PlayFacing(*e, "idle");
    return e;
}

Entity* Spawn_Fisherman(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kFisherman, preset, "beach_npcs");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    FishermanData& d = e->Data<FishermanData>();
    d = FishermanData();
    d.mHealth = 2;
    Ecs_Play(*e, "idle");
    return e;
}

Entity* Spawn_RedWalker(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kRedWalker, preset, "redwalker", 32, 48);
    if (!e)
    {
        return nullptr;
    }
    // GetSprite: "walk" through the 5 frames from a random one
    const int start = Rng_Range(0, 5);
    Ecs_Play(*e, "walk");
    e->mV->mAnimIndex = static_cast<uint16_t>(start);
    e->mT->mVelX = Fx(5 + Rng_Range(0, 15));
    e->mB->mW = 20;
    e->mB->mH = 8;
    e->mV->mOffX = Fx(4);
    e->mV->mOffY = Fx(40);
    e->mT->mX += Fx(4);
    e->mT->mY += Fx(40);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_RedseaNpc(const MapEntity& preset, Entity&)
{
    if (preset.mFrame == 10)
    {
        Entity* e = Spawn_Basic(kBombDude, preset, "redsea_npcs");
        if (!e)
        {
            return nullptr;
        }
        e->mB->mImmovable = true;
        Ecs_Play(*e, "walk");
        Dialog_SetSceneProgress("generic_npc", "bomb", 0);
        return e;
    }
    Entity* e = Spawn_Basic(kHairDude, preset, "redsea_npcs");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_Play(*e, "walk_d");
    return e;
}

Entity* Spawn_Sign(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSign, preset, "note_rock");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_SetFrame(*e, preset.mFrame);
    return e;
}

// ---- Bunny and BunnyRun (Forest/Bunny.cs) ---------------------------------------------------------------------------
namespace
{
void BunnyUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    Ecs_PlayFacing(self, "walk");
}

bool BunnyInteract(Entity&, Facing)
{
    Game_ShowDialogue("forest_npc", "bunny");
    return true;
}

const EntityClass kBunny = {
    "Bunny", BunnyUpdate, nullptr, SeparateFromPlayer, BunnyInteract, nullptr, nullptr, nullptr,
    0,       GROUP_PLAYER, ECF_INTERACTABLE,
};

// Sits there until the player is close, then bolts off the screen the way it faces.
void BunnyRunUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    if (t.mVelX != 0 || t.mVelY != 0)
    {
        const Rect screen = { gGame.mGridX * 160, gGame.mGridY * 160, 160, 160 };
        if (!Ecs_Intersects(screen, Ecs_Hitbox(self)))
        {
            self.mExists = false;
        }
    }
    else if (DistanceToPlayer(self) < Fx(48))
    {
        const Facing f = static_cast<Facing>(self.mFacing);
        t.mVelX = Fx(FacingX(f) * 100);
        t.mVelY = Fx(FacingY(f) * 100);
        Game_PlaySfx("rat_move");
    }
}

const EntityClass kBunnyRun = {
    "BunnyRun", BunnyRunUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};
} // namespace

Entity* Spawn_Bunny(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBunny, preset, "forest_npcs");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_Play(*e, "walk_d");
    return e;
}

Entity* Spawn_BunnyRun(const MapEntity& preset, Entity&)
{
    // (only one in four shows up, each time the screen loads)
    if (Rng_NextDouble() >= 0.25)
    {
        return nullptr;
    }
    Entity* e = Spawn_Basic(kBunnyRun, preset, "forest_npcs");
    if (!e)
    {
        return nullptr;
    }
    e->mFacing = static_cast<uint8_t>(Rng_Range(0, 4));
    Ecs_PlayFacing(*e, "walk");
    return e;
}

// ---- BioFilm (Crowd/BioFilm.cs) --------------------------------------------------------------------------------------
// The membrane the falling player breaks through: until then a DoorToggle keeps the fall door under it shut.
namespace
{
void BioFilmCollided(Entity& self, Entity&)
{
    if (!Preset_Activated(*self.mPreset) && Player_State() == PS_ENTER_FALL && Player_OffsetY() < Fx(16))
    {
        Preset_SetActivated(*self.mPreset, true);
        Ecs_SetFrame(self, 1);
        Game_PlaySfx("broom_hit");
        Spawn_DoorToggle(self.mT->mX, self.mT->mY, 32, 32);
    }
}

const EntityClass kBioFilm = {
    "BioFilm", Ecs_BaseUpdate, nullptr, BioFilmCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};
} // namespace

Entity* Spawn_BioFilm(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBioFilm, preset, "biofilm", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    if (!Preset_Activated(preset))
    {
        Spawn_DoorToggle(e->mT->mX, e->mT->mY, 32, 32);
        Ecs_SetFrame(*e, 0);
    }
    else
    {
        Ecs_SetFrame(*e, 1);
    }
    return e;
}

// ---- Windmill Console (Npc/Windmill/Console.cs) ----------------------------------------------------------------------
// Switching it on plays the cutscene that shows each dungeon's statue sliding aside, then opens the windmill.
namespace
{
enum WindmillStep : uint8_t
{
    WM_START,
    WM_QUIET,     // the song fading out
    WM_FADE_OUT,  // to black
    WM_STATUE,    // the next statue's warp
    WM_SHOW,      // fading in part way
    WM_REVEAL,    // all the way
    WM_MOVE,      // the statue slides
    WM_HIDE,      // back to black
    WM_RETURN,
    WM_FADE_BACK, // in again, with the song
};

struct WindmillConsoleData final
{
    uint8_t mStep;
    uint8_t mStatue; // which dungeon's
    bool mVolumeDone; // VolumeEvent.ReachedTarget
    fx mVolumeTarget;
    fx mVolumeSpeed;
    fx mFade; // gameScreenFade.alpha
    fx mTargetX;
    fx mTargetY;
    Entity* mStatueEntity;
};

const EntityClass kWindmillStatue = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// The coroutine's MoveNext: false at each yield, true once it is done.
bool WindmillCutsceneStep(Entity& self)
{
    struct StatueLoc
    {
        const char* mMap;
        int mGx;
        int mGy;
        int mTx;
        int mTy;
    };
    static const StatueLoc kLocs[3] = {
        { "BEDROOM", 4, 0, 5, 2 },
        { "REDCAVE", 6, 2, 4, 4 },
        { "CROWD", 9, 4, 4, 2 },
    };
    WindmillConsoleData& d = self.Data<WindmillConsoleData>();
    Entity* statue = d.mStatueEntity;
    for (;;)
    {
        switch (d.mStep)
        {
        case WM_START:
            d.mVolumeTarget = 0;
            d.mVolumeSpeed = FxF(0.6f);
            d.mVolumeDone = false;
            d.mStatueEntity = Ecs_Create(kWindmillStatue, 0, 0, PART_VISUAL);
            if (d.mStatueEntity)
            {
                d.mStatueEntity->mCutsceneOwned = true;
                Ecs_MarkSpawned(*d.mStatueEntity);
                Ecs_SetSheet(*d.mStatueEntity, "big_statue", 32, 48);
                d.mStatueEntity->mV->mLayer = LAYER_ENTITIES;
                d.mStatueEntity->mV->mVisible = false;
            }
            d.mStep = WM_QUIET; // yield EntityEvent
            return false;
        case WM_QUIET:
            if (!d.mVolumeDone)
            {
                return false;
            }
            d.mStep = WM_FADE_OUT;
            [[fallthrough]];
        case WM_FADE_OUT:
            if (!MoveTo(d.mFade, kFxOne, FxF(0.6f)))
            {
                return false;
            }
            d.mStatue = 0;
            d.mStep = WM_STATUE;
            [[fallthrough]];
        case WM_STATUE:
        {
            if (d.mStatue == 3)
            {
                if (statue)
                {
                    statue->mExists = false;
                }
                World_CutsceneReturn(); // yield ReturnWarp
                d.mStep = WM_RETURN;
                return false;
            }
            const StatueLoc& loc = kLocs[d.mStatue];
            if (statue)
            {
                statue->mT->mX = Fx(loc.mGx * 160 + loc.mTx * 16);
                statue->mT->mY = Fx(loc.mGy * 160 + loc.mTy * 16);
                Ecs_SetFrame(*statue, d.mStatue);
                statue->mV->mVisible = true;
            }
            World_CutsceneWarp(Game_MapIndex(loc.mMap), loc.mGx, loc.mGy); // yield WarpEvent
            d.mStep = WM_SHOW;
            return false;
        }
        case WM_SHOW:
            if (!MoveTo(d.mFade, FxF(0.45f), FxF(0.6f)))
            {
                return false;
            }
            Game_PlaySfx("red_cave_rise");
            d.mStep = WM_REVEAL;
            [[fallthrough]];
        case WM_REVEAL:
            if (!MoveTo(d.mFade, 0, FxF(0.6f)))
            {
                return false;
            }
            if (statue)
            {
                // DungeonStatue.MoveDir: the bedroom's goes up, the others right
                d.mTargetX = statue->mT->mX + Fx(d.mStatue == 0 ? 0 : 32);
                d.mTargetY = statue->mT->mY - Fx(d.mStatue == 0 ? 32 : 0);
            }
            d.mStep = WM_MOVE;
            [[fallthrough]];
        case WM_MOVE:
            if (statue)
            {
                // (|: both move every frame)
                const bool x = MoveTo(statue->mT->mX, d.mTargetX, Fx(12));
                const bool y = MoveTo(statue->mT->mY, d.mTargetY, Fx(12));
                if (!x || !y)
                {
                    return false;
                }
            }
            Screen_Shake(FxF(0.05f), FxF(0.5f));
            Game_PlaySfx("wb_hit_ground");
            d.mStep = WM_HIDE;
            [[fallthrough]];
        case WM_HIDE:
            if (!MoveTo(d.mFade, kFxOne, FxF(0.6f)))
            {
                return false;
            }
            if (statue)
            {
                statue->mV->mVisible = false;
            }
            d.mStatue++;
            d.mStep = WM_STATUE;
            break;
        case WM_RETURN:
            d.mVolumeTarget = kFxOne;
            d.mVolumeDone = false;
            d.mStep = WM_FADE_BACK;
            [[fallthrough]];
        default:
        {
            // (|: the fade moves every frame)
            const bool faded = MoveTo(d.mFade, 0, FxF(0.6f));
            if (!faded || !d.mVolumeDone)
            {
                return false;
            }
            Game_PlaySong("windmill");
            Event_Inc("WindmillOpened");
            Ecs_FireEvent(GEV_OPENED_WINDMILL);
            if (statue)
            {
                Ecs_Free(*statue);
                d.mStatueEntity = nullptr;
            }
            return true;
        }
        }
    }
}

// The cutscene's frame: its step, then its own entities (the VolumeEvent) update; the screen fade follows.
bool WindmillCutscene(Entity& self)
{
    WindmillConsoleData& d = self.Data<WindmillConsoleData>();
    const bool done = WindmillCutsceneStep(self);
    if (!d.mVolumeDone)
    {
        fx volume = FxF(Game_SongVolume());
        d.mVolumeDone = MoveTo(volume, d.mVolumeTarget, d.mVolumeSpeed);
        Game_SetSongVolume(volume.mV);
    }
    Screen_SetFadeWhite(false); // (fadeColor = Black before its fade, which was 0 until then)
    Screen_SetFade(d.mFade);
    return done;
}

bool WindmillConsoleInteract(Entity& self, Facing)
{
    if (Ecs_AnimIs(self, "active"))
    {
        WindmillConsoleData& d = self.Data<WindmillConsoleData>();
        d = WindmillConsoleData();
        Game_StartCutscene(self, WindmillCutscene);
        Ecs_Play(self, "idle");
    }
    Game_PlaySfx("get_small_health");
    return true;
}

const EntityClass kWindmillConsole = {
    "Console", Ecs_BaseUpdate, nullptr, nullptr, WindmillConsoleInteract, nullptr, nullptr, nullptr,
    0,         0,              ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_WindmillConsole(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kWindmillConsole, preset, "windmill_inside", 48, 48);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_Play(*e, "idle");
    if (Event_Get("WindmillOpened") == 0)
    {
        Ecs_Play(*e, "active");
    }
    e->mV->mOffX = e->mV->mOffY = Fx(16);
    e->mT->mX += Fx(16);
    e->mT->mY += Fx(16);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    return e;
}

// ---- The Suburb's people (Npc/Suburb/*.cs, Enemy/Suburb/SuburbKiller.cs, QuestNPCs/SuburbQuest.cs) -------------------
namespace
{
// FaceTowards(Position - FacingDirection(playerFacing)): turn to the player who's talking.
void FacePlayerTalking(Entity& self, Facing playerFacing)
{
    Ecs_FaceTowards(self, self.mT->mX - Fx(FacingX(playerFacing)), self.mT->mY - Fx(FacingY(playerFacing)));
}

void SeparateFromAny(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

// ---- SuburbWalker: walks round in squares until the broom finds it
struct SuburbWalkerData final
{
    float mWalkMax;
    float mWalk;
    Entity* mBlood;
};

void SuburbWalkerSpawnBlood(Entity& self)
{
    Entity& blood = *self.Data<SuburbWalkerData>().mBlood;
    blood.mExists = true;
    const int x = Rng_Range(-3, 4);
    const int y = Rng_Range(-3, 4);
    blood.mT->mX = self.mT->mX + Fx(x);
    blood.mT->mY = self.mT->mY + Fx(y);
}

void SuburbWalkerUpdate(Entity& self)
{
    SuburbWalkerData& d = self.Data<SuburbWalkerData>();
    Transform& t = *self.mT;
    Ecs_BaseUpdate(self);
    if (Preset_Alive(*self.mPreset))
    {
        if (self.mB->mTouching != TOUCH_NONE)
        {
            d.mWalk = 0;
        }
        const float gx = InGrid(t.mX);
        const float gy = InGrid(t.mY);
        if ((gx < 5 && t.mVelX < 0) || (gx + static_cast<float>(self.mB->mW) > 155 && t.mVelX > 0) ||
            (gy < 5 && t.mVelY < 0) || (gy + static_cast<float>(self.mB->mH) > 155 && t.mVelY > 0))
        {
            d.mWalk = 0;
        }
        d.mWalk -= kDeltaTime;
        if (d.mWalk <= 0)
        {
            const fx vx = t.mVelX;
            t.mVelX = t.mVelY;
            t.mVelY = -vx;
            Ecs_FaceTowards(self, t.mX + t.mVelX, t.mY + t.mVelY);
            Ecs_PlayFacing(self, "walk");
            d.mWalk = d.mWalkMax;
        }
    }
    else if (Ecs_AnimFinished(self) && !d.mBlood->mExists)
    {
        SuburbWalkerSpawnBlood(self);
    }
}

bool SuburbWalkerInteract(Entity& self, Facing)
{
    if (Ecs_Frame(*self.Data<SuburbWalkerData>().mBlood) != 66)
    {
        return false;
    }
    const int frame = self.mPreset->mFrame;
    Game_ShowRandomDialogue("suburb_walker", frame < 2 ? "words_adult" : (frame < 4 ? "words_teen" : "words_kid"));
    return true;
}

void SuburbWalkerCollided(Entity& self, Entity& other)
{
    if (&other == World_Player())
    {
        // (pushes the player out without being pushed, and keeps its own touching)
        self.mB->mImmovable = true;
        const uint8_t old = self.mB->mTouching;
        Ecs_Separate(self, other);
        self.mB->mTouching = old;
        self.mB->mImmovable = false;
    }
    else if (Preset_Alive(*self.mPreset))
    {
        Preset_SetAlive(*self.mPreset, false);
        self.mT->mVelX = self.mT->mVelY = 0;
        Ecs_Play(self, "die");
        Game_PlaySfx("broom_hit");
        Game_PlaySfx("fall_in_hole");
        Event_Inc("SuburbKilled");
    }
}

const EntityClass kSuburbWalker = {
    "SuburbWalker", SuburbWalkerUpdate, nullptr, SuburbWalkerCollided, SuburbWalkerInteract, nullptr, nullptr, nullptr,
    0, GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION | ECF_ALWAYS_SPAWN | ECF_INTERACTABLE,
};

const EntityClass kSuburbPart = {
    "Entity", Ecs_BaseUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- SuburbKiller: stands still until the player comes close, then follows
struct SuburbKillerData final
{
    bool mMoving;
    float mMoveTimer;
};

void SuburbKillerUpdate(Entity& self)
{
    SuburbKillerData& d = self.Data<SuburbKillerData>();
    const Entity& p = *World_Player();
    Ecs_BaseUpdate(self);
    Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    if (!d.mMoving)
    {
        Ecs_PlayFacing(self, "idle");
        d.mMoving = FxLength(self.mT->mX - p.mT->mX, self.mT->mY - p.mT->mY) < Fx(36);
    }
    else
    {
        Ecs_PlayFacing(self, "walk");
        d.mMoveTimer += kDeltaTime;
        if (d.mMoveTimer > 0.5f)
        {
            d.mMoveTimer = 0;
            Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(30));
        }
    }
}

void SuburbKillerCollided(Entity&, Entity& other)
{
    if (&other == World_Player())
    {
        Player_ReceiveDamage(6);
    }
}

const EntityClass kSuburbKiller = {
    "SuburbKiller", SuburbKillerUpdate, nullptr, SuburbKillerCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,       0,
};

// ---- SuburbBlocker: won't let the player by until enough of the Suburb is dead
struct SuburbBlockerData final
{
    bool mDying;
};

void SuburbBlockerUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (self.Data<SuburbBlockerData>().mDying && MoveTo(self.mV->mOpacity, 0, FxF(0.6f)))
    {
        self.mExists = false;
    }
}

bool SuburbBlockerInteract(Entity& self, Facing playerFacing)
{
    SuburbBlockerData& d = self.Data<SuburbBlockerData>();
    if (d.mDying)
    {
        return false;
    }
    FacePlayerTalking(self, playerFacing);
    Ecs_PlayFacing(self, "walk");
    const int killed = Event_Get("SuburbKilled");
    const int dialog = killed < 6 ? 0 : (killed < 9 ? 1 : (killed < 10 ? 2 : 3));
    Game_ShowDialogue("suburb_blocker", "one", dialog);
    if (dialog == 3)
    {
        Ecs_Flicker(self, FxSec(0.5f));
        d.mDying = true;
        Preset_SetAlive(*self.mPreset, false);
    }
    return true;
}

const EntityClass kSuburbBlocker = {
    "SuburbBlocker", SuburbBlockerUpdate, nullptr, SeparateFromAny, SuburbBlockerInteract, nullptr, nullptr, nullptr,
    0,               GROUP_PLAYER,        ECF_INTERACTABLE,
};

// ---- SuburbIndoors: the people at home (SuburbAlive turns to talk; SuburbDead and SuburbHanged don't)
struct SuburbIndoorsData final
{
    const char* mScene;
    int8_t mLine;
    bool mTurns;
};

bool SuburbIndoorsInteract(Entity& self, Facing playerFacing)
{
    const SuburbIndoorsData& d = self.Data<SuburbIndoorsData>();
    if (d.mTurns)
    {
        FacePlayerTalking(self, playerFacing);
        Ecs_PlayFacing(self, "walk");
    }
    Game_ShowDialogue("suburb_walker", d.mScene, d.mLine);
    return true;
}

const EntityClass kSuburbAlive = {
    "SuburbAlive", Ecs_BaseUpdate, nullptr, SeparateFromAny, SuburbIndoorsInteract, nullptr, nullptr, nullptr,
    0,             GROUP_PLAYER,   ECF_INTERACTABLE,
};
const EntityClass kSuburbDead = {
    "SuburbDead", Ecs_BaseUpdate, nullptr, SeparateFromAny, SuburbIndoorsInteract, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER,   ECF_INTERACTABLE,
};
const EntityClass kSuburbHanged = {
    "SuburbHanged", Ecs_BaseUpdate, nullptr, SeparateFromAny, SuburbIndoorsInteract, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,   ECF_INTERACTABLE,
};

// ---- SuburbQuest
struct SuburbQuestData final
{
    bool mPlayedQuest;
};

bool SuburbQuestInteract(Entity& self, Facing)
{
    SuburbQuestData& d = self.Data<SuburbQuestData>();
    const int progress = Event_Get("GoQuestProgress");
    if ((progress == 5 || progress == 6) && !d.mPlayedQuest)
    {
        d.mPlayedQuest = true;
        Game_ShowDialogue("generic_npc", "quest_event");
        if (progress == 5)
        {
            Event_Inc("GoQuestProgress");
        }
    }
    else
    {
        Game_ShowDialogue("generic_npc", "quest_normal");
    }
    return true;
}

const EntityClass kSuburbQuest = {
    "SuburbQuest", Ecs_BaseUpdate, nullptr, SeparateFromAny, SuburbQuestInteract, nullptr, nullptr, nullptr,
    0,             GROUP_PLAYER,   ECF_INTERACTABLE,
};

// ---- The other Go quest NPCs (QuestNPCs/*.cs): each one's quest_event line moves GoQuestProgress on from its step
// (`step`: it also says it once at step + 1, if not said yet); otherwise quest_normal, or second (`helper`: the
// QuestNPCHelper name, nullptr for the ones without a second line).
struct QuestData final
{
    bool mPlayedQuest;
};

void QuestTalk(Entity& self, int step, const char* helper)
{
    QuestData& d = self.Data<QuestData>();
    const int progress = Event_Get("GoQuestProgress");
    if ((progress == step || progress == step + 1) && !d.mPlayedQuest)
    {
        d.mPlayedQuest = true;
        Game_ShowDialogue("generic_npc", "quest_event");
        if (progress == step)
        {
            Event_Inc("GoQuestProgress");
        }
    }
    else if (helper && NeedsSecond(helper, "quest_normal", "second"))
    {
        Game_ShowDialogue("generic_npc", "second");
    }
    else
    {
        Game_ShowDialogue("generic_npc", "quest_normal");
    }
}

bool CliffQuestInteract(Entity& self, Facing)
{
    // (the move anim plays for the normal lines only)
    QuestData& d = self.Data<QuestData>();
    const int progress = Event_Get("GoQuestProgress");
    if (!((progress == 0 || progress == 1) && !d.mPlayedQuest))
    {
        Ecs_Play(self, "move");
    }
    QuestTalk(self, 0, "cliff");
    return true;
}

bool BeachQuestInteract(Entity& self, Facing)
{
    QuestTalk(self, 1, "beach");
    Ecs_Play(self, "turn");
    return true;
}

bool ForestQuestInteract(Entity& self, Facing)
{
    QuestTalk(self, 2, "forest");
    return true;
}

bool FieldsQuestInteract(Entity& self, Facing)
{
    QuestTalk(self, 3, nullptr);
    return true;
}

bool CellQuestInteract(Entity& self, Facing playerFacing)
{
    self.mFacing = FlipFacing(playerFacing);
    Ecs_PlayFacing(self, "idle");
    QuestTalk(self, 4, nullptr);
    return true;
}

bool SpaceQuestInteract(Entity& self, Facing)
{
    QuestTalk(self, 6, nullptr);
    return true;
}

bool GoQuestInteract(Entity& self, Facing)
{
    QuestData& d = self.Data<QuestData>();
    if (Event_Get("GoQuestProgress") == 7 && !d.mPlayedQuest)
    {
        d.mPlayedQuest = true;
        Game_ShowDialogue("generic_npc", "quest_event");
    }
    else
    {
        Game_ShowDialogue("generic_npc", "quest_normal");
    }
    return true;
}

#define QUEST_CLASS(var, name, interact)                                                                              \
    const EntityClass var = {                                                                                          \
        name, Ecs_BaseUpdate, nullptr, SeparateFromAny, interact, nullptr, nullptr, nullptr, 0, GROUP_PLAYER,          \
        ECF_INTERACTABLE,                                                                                              \
    }
QUEST_CLASS(kCliffQuest, "CliffQuest", CliffQuestInteract);
QUEST_CLASS(kBeachQuest, "BeachQuest", BeachQuestInteract);
QUEST_CLASS(kForestQuest, "ForestQuest", ForestQuestInteract);
QUEST_CLASS(kFieldsQuest, "FieldsQuest", FieldsQuestInteract);
QUEST_CLASS(kCellQuest, "CellQuest", CellQuestInteract);
QUEST_CLASS(kSpaceQuest, "SpaceQuest", SpaceQuestInteract);
QUEST_CLASS(kGoQuest, "GoQuest", GoQuestInteract);
#undef QUEST_CLASS

// FieldsQuest.Bush: the broom on it gets a line.
void QuestBushCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        Ecs_Separate(self, other);
    }
    else if (!Dialog_IsOpen())
    {
        Game_ShowDialogue("generic_npc", "bush");
    }
}

const EntityClass kQuestBush = {
    "Bush", Ecs_BaseUpdate, nullptr, QuestBushCollided, nullptr, nullptr, nullptr, nullptr,
    0,      GROUP_PLAYER | GROUP_BROOM, 0,
};

// ---- CubeKing (Npc/Space/CubeKing.cs), FieldsEaster (Npc/Fields/FieldsEaster.cs), Thorax (Npc/Forest/Thorax.cs) -------
bool CubeKingInteract(Entity& self, Facing)
{
    Game_ShowDialogue("cube_king", self.mPreset->mFrame == 10 ? "gray" : "color");
    return true;
}

const EntityClass kCubeKing = {
    "CubeKing", Ecs_BaseUpdate, nullptr, SeparateFromAny, CubeKingInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER,   ECF_INTERACTABLE,
};

bool FieldsEasterInteract(Entity& self, Facing)
{
    // (the area given: Marvin, frame 16, was moved to the Beach)
    static const char* const kScenes[4] = { "hamster", "chikapu", "electric", "marvin" };
    const int i = self.mPreset->mFrame - 13;
    Game_ShowDialogueFor("generic_npc", "FIELDS", i >= 0 && i < 4 ? kScenes[i] : "ERROR");
    return true;
}

const EntityClass kFieldsEaster = {
    "FieldsEaster", Ecs_BaseUpdate, nullptr, SeparateFromAny, FieldsEasterInteract, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,   ECF_INTERACTABLE,
};

bool ThoraxInteract(Entity&, Facing)
{
    Game_ShowDialogue("forest_npc", "thorax");
    return true;
}

const EntityClass kThorax = {
    "Thorax", Ecs_BaseUpdate, nullptr, SeparateFromAny, ThoraxInteract, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER,   ECF_INTERACTABLE,
};

// ---- Mushroom (Npc/Forest/Mushroom.cs): wobbles and plays a note when the broom hits it
void MushroomUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimIs(self, "move") && Ecs_AnimFinished(self))
    {
        Ecs_Play(self, "idle@2");
    }
}

void MushroomCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        Ecs_Separate(self, other);
    }
    else if (!Ecs_AnimIs(self, "move"))
    {
        Ecs_Play(self, "move");
        static const char* const kSfx[3] = { "cross2", "cross3", "cross4" };
        Game_PlaySfxOneOf(kSfx, 3);
    }
}

const EntityClass kMushroom = {
    "Mushroom", MushroomUpdate, nullptr, MushroomCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_BROOM, 0,
};

// ---- Olive (Npc/Fields/Olive.cs): hops right and back again
struct OliveData final
{
    uint8_t mStep;
    float mT;
};

void OliveUpdate(Entity& self)
{
    OliveData& d = self.Data<OliveData>();
    Transform& t = *self.mT;
    Ecs_BaseUpdate(self);
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            d.mT = 0;
            t.mVelX = t.mVelY = 0;
            Ecs_Play(self, "idle");
            self.mV->mHFlip = false;
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (d.mT < 1.4f)
            {
                d.mT += kDeltaTime;
                return;
            }
            Ecs_Play(self, "hop");
            t.mVelX = Fx(25);
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (!Ecs_AnimFinished(self))
            {
                return;
            }
            t.mVelX = t.mVelY = 0;
            Ecs_Play(self, "idle");
            d.mT = 0;
            d.mStep = 3;
            [[fallthrough]];
        case 3:
            if (d.mT < 1.6f)
            {
                d.mT += kDeltaTime;
                return;
            }
            self.mV->mHFlip = true;
            t.mVelX = -Fx(25);
            Ecs_Play(self, "hop");
            d.mStep = 4;
            [[fallthrough]];
        default:
            if (!Ecs_AnimFinished(self))
            {
                return;
            }
            d.mStep = 0;
            break;
        }
    }
}

bool OliveInteract(Entity&, Facing)
{
    Game_ShowDialogue("generic_npc", "easter_egg");
    return true;
}

const EntityClass kOlive = {
    "Olive", OliveUpdate,  nullptr, SeparateFromAny, OliveInteract, nullptr, nullptr, nullptr,
    0,       GROUP_PLAYER, ECF_MAP_COLLISION | ECF_INTERACTABLE,
};

// ---- ForestBriar, BeachBriar (Npc/Forest/ForestBriar.cs, Npc/Beach/BeachBriar.cs): the Briar's shadow, walking off
void ForestBriarUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    MoveTo(self.mV->mOpacity, kFxOne, FxF(0.3f));
    const Rect screen = { gGame.mGridX * 160, gGame.mGridY * 160, 160, 160 };
    if (!Ecs_Intersects(screen, Ecs_Hitbox(self)))
    {
        self.mExists = false;
        Preset_SetAlive(*self.mPreset, false);
    }
}

void BeachBriarUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Preset_Alive(*self.mPreset))
    {
        if (MoveTo(self.mV->mOpacity, kFxOne, FxF(0.42f)))
        {
            self.mT->mVelY = 0;
            Ecs_Play(self, "idle_d");
        }
    }
    else if (MoveTo(self.mV->mOpacity, 0, FxF(0.6f)))
    {
        self.mExists = false;
    }
}

void BeachBriarCollided(Entity& self, Entity&)
{
    if (self.mT->mVelY == 0)
    {
        self.mT->mVelY = Fx(15);
        Preset_SetAlive(*self.mPreset, false);
        Ecs_Play(self, "walk_d@2");
    }
}

const EntityClass kForestBriar = {
    "ForestBriar", ForestBriarUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

const EntityClass kBeachBriar = {
    "BeachBriar", BeachBriarUpdate, nullptr, BeachBriarCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

const EntityClass kCrowdRail = {
    "CrowdRail", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* SpawnQuest(const EntityClass& cls, const MapEntity& preset, const char* sheet, const char* anim,
                   int frameW = 16, int frameH = 16)
{
    Entity* e = Spawn_Basic(cls, preset, sheet, frameW, frameH);
    if (!e)
    {
        return nullptr;
    }
    if (anim)
    {
        Ecs_Play(*e, anim);
    }
    e->mB->mImmovable = true;
    e->Data<QuestData>() = QuestData();
    return e;
}
} // namespace

Entity* Spawn_SuburbWalker(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSuburbWalker, preset, "suburb_walkers");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, preset.mFrame * 9);
    Ecs_Play(*e, "walk_d");
    e->mT->mVelY = Fx(20);
    SuburbWalkerData& d = e->Data<SuburbWalkerData>();
    d = SuburbWalkerData();
    d.mWalkMax = 1.0f + static_cast<float>(Rng_NextDouble());
    d.mWalk = d.mWalkMax;
    d.mBlood = Ecs_Create(kSuburbPart, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
    if (!d.mBlood)
    {
        return nullptr;
    }
    Ecs_SetSheet(*d.mBlood, "suburb_walkers");
    d.mBlood->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_Play(*d.mBlood, "a@3");
    Ecs_KeepPooled(*d.mBlood);
    if (!Preset_Alive(preset))
    {
        e->mT->mVelX = e->mT->mVelY = 0;
        Ecs_Play(*e, "dead");
        SuburbWalkerSpawnBlood(*e);
    }
    return e;
}

Entity* Spawn_SuburbKiller(const MapEntity& preset, Entity&)
{
    const int look = Rng_Range(0, 7);
    Entity* e = Spawn_Basic(kSuburbKiller, preset, "suburb_killers");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, look * 9);
    Ecs_Play(*e, "idle_d");
    e->mB->mW = e->mB->mH = 6;
    e->mV->mOffX = e->mV->mOffY = Fx(5);
    e->mT->mX += Fx(5);
    e->mT->mY += Fx(5);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->Data<SuburbKillerData>() = SuburbKillerData();
    return e;
}

Entity* Spawn_SuburbBlocker(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSuburbBlocker, preset, "suburb_walkers");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "walk_d");
    e->mB->mImmovable = true;
    e->Data<SuburbBlockerData>() = SuburbBlockerData();
    return e;
}

Entity* Spawn_SuburbIndoors(const MapEntity& preset, Entity&)
{
    const int frame = preset.mFrame;
    const EntityClass& cls = frame == 6 ? kSuburbHanged : ((frame == 4 || frame == 7) ? kSuburbDead : kSuburbAlive);
    Entity* e = Spawn_Basic(cls, preset, "suburb_walkers");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    SuburbIndoorsData& d = e->Data<SuburbIndoorsData>();
    d = SuburbIndoorsData();
    d.mLine = -1;
    if (&cls == &kSuburbHanged)
    {
        Ecs_Play(*e, "hanged");
        d.mScene = "hanged";
        for (int i = 0; i < 3; i++)
        {
            Entity* r = Ecs_Create(kSuburbPart, e->mT->mX, e->mT->mY - Fx(16 * (i + 1)), PART_VISUAL | PART_BRAIN,
                                   nullptr, e);
            if (r)
            {
                Ecs_SetSheet(*r, "suburb_walkers");
                r->mV->mLayer = LAYER_ENTITIES;
                Ecs_Play(*r, "a@2");
            }
        }
    }
    else if (&cls == &kSuburbDead)
    {
        Ecs_SetFrameBase(*e, frame == 4 ? 9 : 0);
        Ecs_Play(*e, "dead");
        d.mScene = "dead";
        d.mLine = frame == 4 ? 0 : 1;
    }
    else
    {
        static const int kOffset[6] = { 0, 4, 1, 2, 0, 0 };
        Ecs_SetFrameBase(*e, kOffset[frame < 6 ? frame : 0] * 9);
        Ecs_Play(*e, "walk_d");
        d.mTurns = true;
        switch (frame)
        {
        case 0:
            d.mScene = "paranoid";
            break;
        case 1:
            d.mScene = "family";
            d.mLine = 1;
            break;
        case 2:
            d.mScene = "family";
            d.mLine = 0;
            break;
        case 3:
            d.mScene = "older_kid";
            break;
        default:
            d.mScene = "festive";
            break;
        }
    }
    return e;
}

Entity* Spawn_CliffQuest(const MapEntity& preset, Entity&)
{
    return SpawnQuest(kCliffQuest, preset, "cliffs_npcs", "a");
}

Entity* Spawn_BeachQuest(const MapEntity& preset, Entity&)
{
    return SpawnQuest(kBeachQuest, preset, "beach_npcs", "a");
}

Entity* Spawn_ForestQuest(const MapEntity& preset, Entity&)
{
    return SpawnQuest(kForestQuest, preset, "forest_npcs", "a");
}

Entity* Spawn_FieldsQuest(const MapEntity& preset, Entity&)
{
    Entity* e = SpawnQuest(kFieldsQuest, preset, "fields_npcs", "a", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 16;
    e->mT->mY += Fx(16);
    e->mT->mLastY = e->mT->mY;
    e->mV->mOffY = Fx(16);
    Entity* bush = Ecs_Create(kQuestBush, e->mT->mX + Fx(16), e->mT->mY, PART_VISUAL);
    if (bush)
    {
        bush->mB->mW = bush->mB->mH = 16;
        bush->mB->mImmovable = true;
        Ecs_SetSheet(*bush, "fields_npcs", 16, 16);
        Ecs_SetFrame(*bush, 30);
        bush->mV->mLayer = LAYER_BG_ENTITIES;
    }
    return e;
}

Entity* Spawn_CellQuest(const MapEntity& preset, Entity&)
{
    return SpawnQuest(kCellQuest, preset, "cell_bodies", "idle_d");
}

Entity* Spawn_SpaceQuest(const MapEntity& preset, Entity&)
{
    return SpawnQuest(kSpaceQuest, preset, "space_npcs", "a@3");
}

Entity* Spawn_GoQuest(const MapEntity& preset, Entity&)
{
    return SpawnQuest(kGoQuest, preset, "note_rock", nullptr);
}

Entity* Spawn_CubeKing(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kCubeKing, preset, "space_npcs", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, preset.mFrame);
    Ecs_Play(*e, "a");
    // (a 32x32 sprite: the hitbox is as big)
    e->mB->mW = 32;
    e->mB->mH = 32;
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_FieldsEaster(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kFieldsEaster, preset, "fields_npcs");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, preset.mFrame * 2 + 80 - 26);
    Ecs_Play(*e, "a@2");
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_Thorax(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kThorax, preset, "forest_npcs");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "move@2");
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_Mushroom(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kMushroom, preset, "forest_npcs");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "idle@2");
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_Olive(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kOlive, preset, "forest_npcs");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "idle");
    e->mB->mImmovable = true;
    e->Data<OliveData>() = OliveData();
    return e;
}

namespace
{
Entity* SpawnShadowBriar(const MapEntity& preset, bool beach)
{
    Entity* e = Spawn_Basic(beach ? kBeachBriar : kForestBriar, preset, "briar");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, beach ? "walk_d@2" : "walk_u@2");
    e->mV->mOpacity = 0;
    e->mT->mVelY = beach ? Fx(20) : -Fx(20);
    return e;
}
} // namespace

Entity* Spawn_ForestBriar(const MapEntity& preset, Entity&)
{
    return SpawnShadowBriar(preset, false);
}

Entity* Spawn_BeachBriar(const MapEntity& preset, Entity&)
{
    return SpawnShadowBriar(preset, true);
}

Entity* Spawn_CrowdRail(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kCrowdRail, preset, "rail_CROWD", 16, 22, PART_VISUAL);
    if (e)
    {
        Ecs_SetSolid(*e, false);
    }
    return e;
}

Entity* Spawn_SuburbQuest(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSuburbQuest, preset, "suburb_walkers");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "a");
    e->mB->mImmovable = true;
    e->Data<SuburbQuestData>() = SuburbQuestData();
    return e;
}

// ---- Rail (Decorations/NonSolid.cs), ApartmentEaster and RedCaveEaster (Npc/EasterEggs) -------------------------
namespace
{
const EntityClass kRail = {
    "Rail", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

bool EasterInteract(Entity&, Facing)
{
    Game_ShowDialogue("generic_npc", "easter_egg");
    return true;
}

const EntityClass kApartmentEaster = {
    "ApartmentEaster", nullptr, nullptr, SeparateFromAny, EasterInteract, nullptr, nullptr, nullptr,
    0,                 GROUP_PLAYER, ECF_INTERACTABLE,
};

const EntityClass kRedCaveEaster = {
    "RedCaveEaster", nullptr, nullptr, SeparateFromAny, EasterInteract, nullptr, nullptr, nullptr,
    0,               GROUP_PLAYER, ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_Rail(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kRail, preset, "rail", 16, 16, PART_VISUAL);
    if (e)
    {
        Ecs_SetFrame(*e, 0);
    }
    return e;
}

Entity* Spawn_EasterEgg(const MapEntity& preset, Entity&)
{
    const bool redcave = gGame.mMapName && strcmp(gGame.mMapName, "REDCAVE") == 0;
    const int size = redcave ? 32 : 16;
    Entity* e = Spawn_Basic(redcave ? kRedCaveEaster : kApartmentEaster, preset, redcave ? "smoke_red" : "randoms",
                            size, size);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrame(*e, 0);
    e->mB->mW = e->mB->mH = static_cast<int16_t>(size);
    e->mB->mImmovable = true;
    return e;
}

// ---- HotelGuy (Npc/Hotel/HotelGuy.cs) ------------------------------------------------------------------------------
namespace
{
bool HotelGuyInteract(Entity& self, Facing playerFacing)
{
    FacePlayerTalking(self, playerFacing);
    Ecs_PlayFacing(self, "idle");
    Game_ShowDialogue("generic_npc", "one");
    return true;
}

const EntityClass kHotelGuy = {
    "HotelGuy", nullptr, nullptr, SeparateFromAny, HotelGuyInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER, ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_HotelGuy(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kHotelGuy, preset, "hotel_npcs");
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "idle_u");
    e->mB->mImmovable = true;
    return e;
}

// ---- SpaceNPC (Npc/Space/SpaceNPC.cs): the geoms, gray (frames 0-9) and colour (10-) ------------------------------
namespace
{
struct SpaceNpcData final
{
    const char* mDialogue;
};

bool SpaceNpcInteract(Entity& self, Facing playerFacing)
{
    if (strncmp(Ecs_AnimName(self), "walk", 4) == 0)
    {
        FacePlayerTalking(self, playerFacing);
        Ecs_PlayFacing(self, "walk");
    }
    Game_ShowDialogue("geoms", self.Data<SpaceNpcData>().mDialogue);
    return true;
}

const EntityClass kSpaceNpc = {
    "SpaceNPC", nullptr, nullptr, SeparateFromAny, SpaceNpcInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER, ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_SpaceNpc(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSpaceNpc, preset, "space_npcs");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_SetFrameBase(*e, preset.mFrame >= 10 ? 10 : 0);
    Ecs_Play(*e, "walk_d");
    const char* dialogue = "";
    switch (preset.mFrame)
    {
    case 0:
        dialogue = "gray1";
        break;
    case 1:
        dialogue = "gray2";
        break;
    case 3:
        dialogue = "gray3";
        break;
    case 10:
        dialogue = "color1";
        break;
    case 11:
        dialogue = "color2";
        break;
    case 12:
        dialogue = "color3";
        break;
    case 2:
        dialogue = "grayspin";
        Ecs_Play(*e, "spin");
        break;
    case 8:
        dialogue = "colordead";
        Ecs_Play(*e, "dead");
        break;
    case 18:
        dialogue = "graydead";
        Ecs_Play(*e, "dead");
        break;
    default:
        break;
    }
    e->Data<SpaceNpcData>().mDialogue = dialogue;
    return e;
}

// ---- CliffDog (Npc/CliffDog.cs): the dog on the cliffs, wandering left and right now and then
namespace
{
struct CliffDogData final
{
    float mTimer;
};

void CliffDogUpdate(Entity& self)
{
    CliffDogData& d = self.Data<CliffDogData>();
    Transform& t = *self.mT;
    d.mTimer += kDeltaTime;
    if (d.mTimer > 1)
    {
        if (strcmp(Ecs_AnimName(self), "walk") == 0)
        {
            Ecs_FaceTowards(self, t.mX - t.mVelX, t.mY - t.mVelY);
            t.mVelX = 0;
            t.mVelY = 0;
            Ecs_Play(self, "stop");
            self.mV->mHFlip = false;
        }
        else if (Rng_NextDouble() > 0.6)
        {
            const Facing f = static_cast<Facing>(self.mFacing);
            t.mVelX = Fx(15 * FacingX(f));
            t.mVelY = Fx(15 * FacingY(f));
            Ecs_Play(self, "walk", false, 8);
            if (f == FACE_LEFT)
            {
                self.mV->mHFlip = true;
            }
        }
        d.mTimer = 0;
    }
    Ecs_BaseUpdate(self);
}

bool CliffDogInteract(Entity&, Facing)
{
    Game_ShowDialogue("cliff_dog", "top_left");
    return true;
}

const EntityClass kCliffDog = {
    "CliffDog", CliffDogUpdate, nullptr, SeparateFromAny, CliffDogInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER,   ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_CliffDog(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kCliffDog, preset, "dog");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_Play(*e, "stop");
    e->mFacing = FACE_RIGHT;
    e->Data<CliffDogData>() = CliffDogData();
    return e;
}

// ---- Shell (Npc/Windmill/Shell.cs): the windmill's hub, with its three blades turning once the windmill is opened
// Each blade is a 192x192 frame turned 0/90/180/270 degrees about its middle: the sheet has every frame turned a
// quarter (frames 24-47), and flips make the other two turns.
namespace
{
struct BladeData final
{
    fx mCenterX;
    fx mCenterY;
    int16_t mBaseAngle;
    int16_t mSwapIndex;
    const char* mRotate; // its "rotate" anim
};

void BladeSetPos(Entity& self, int angle)
{
    const BladeData& d = self.Data<BladeData>();
    Visual& v = *self.mV;
    Ecs_SetFrameBase(self, (angle == 90 || angle == 270) ? 24 : 0);
    v.mHFlip = angle >= 180;
    v.mVFlip = angle >= 180;
    fx x = d.mCenterX;
    fx y = d.mCenterY;
    if (angle == 0 || angle == 270)
    {
        y -= Fx(192 - 48);
    }
    if (angle == 180 || angle == 270)
    {
        x -= Fx(192 - 48);
    }
    self.mT->mX = x;
    self.mT->mY = y;
}

void BladePostUpdate(Entity& self)
{
    const BladeData& d = self.Data<BladeData>();
    BladeSetPos(self, Ecs_AnimIndex(self) >= d.mSwapIndex ? (d.mBaseAngle + 270) % 360 : d.mBaseAngle);
}

void BladeOnEvent(Entity& self, int gameEvent)
{
    if (gameEvent == GEV_OPENED_WINDMILL)
    {
        Ecs_Play(self, self.Data<BladeData>().mRotate);
    }
    else if (gameEvent == GEV_END_SCREEN_TRANSITION)
    {
        self.mV->mVisible = true;
    }
}

const EntityClass kShell = {
    "Shell", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBlade = {
    "Blade", nullptr, BladePostUpdate, nullptr, nullptr, nullptr, BladeOnEvent, nullptr, 0, 0, 0,
};
} // namespace

Entity* Spawn_Shell(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kShell, Fx(160 + 32), Fx(320 + 48), PART_VISUAL, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 48;
    e->mB->mH = 48;
    Ecs_SetSheet(*e, "windmill_shell", 48, 48);
    e->mV->mLayer = LAYER_FG_SPRITES;
    struct BladeSpec
    {
        int16_t mAngle;
        int16_t mStart;
        const char* mIdle;
        const char* mRotate;
    };
    static const BladeSpec kBlades[3] = {
        { 0, 0, "idle", "rotate" },
        { 270, 8, "idle@8", "rotate@8" },
        { 180, 16, "idle@16", "rotate@16" },
    };
    for (const BladeSpec& spec : kBlades)
    {
        Entity* b = Ecs_Create(kBlade, e->mT->mX, e->mT->mY, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!b)
        {
            continue;
        }
        b->mB->mW = 192;
        b->mB->mH = 192;
        Ecs_SetSheet(*b, "windmill_blade", 192, 192);
        b->mV->mLayer = LAYER_FOOT_OVERLAY;
        b->mV->mVisible = false;
        Ecs_Play(*b, spec.mIdle);
        BladeData& d = b->Data<BladeData>();
        d.mCenterX = e->mT->mX;
        d.mCenterY = e->mT->mY;
        d.mBaseAngle = spec.mAngle;
        d.mSwapIndex = static_cast<int16_t>(24 - spec.mStart);
        d.mRotate = spec.mRotate;
        if (Event_Get("WindmillOpened") != 0)
        {
            Ecs_Play(*b, spec.mRotate);
        }
    }
    return e;
}

// ---- CellBody (Npc/Cell/CellBody.cs): a body lying in the cell, just a picture
namespace
{
const EntityClass kCellBody = {
    "CellBody", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};
} // namespace

namespace
{
// ---- Snowman (Npc/Blue/Snowman.cs): says its line (when spoken to, or by itself after 3 s), then melts --------------
struct SnowmanData final
{
    float mIdleTimer;
    int mDialogue;
    bool mTalked;
};

void SnowmanUpdate(Entity& self)
{
    SnowmanData& d = self.Data<SnowmanData>();
    Ecs_BaseUpdate(self);
    if (d.mTalked)
    {
        if (Game_DialogueFinished() && Ecs_Frame(self) != 6)
        {
            Ecs_Play(self, "melt");
        }
    }
    else if (Preset_Alive(*self.mPreset))
    {
        d.mIdleTimer -= kDeltaTime;
        if (d.mIdleTimer <= 0)
        {
            Game_ShowDialogue("generic_npc", "one", d.mDialogue + 1);
            d.mTalked = true;
            Preset_SetAlive(*self.mPreset, false);
        }
    }
}

void SnowmanCollided(Entity& self, Entity& other)
{
    if (Ecs_Frame(self) != 6)
    {
        Ecs_Separate(self, other);
    }
}

bool SnowmanInteract(Entity& self, Facing)
{
    SnowmanData& d = self.Data<SnowmanData>();
    if (!Preset_Alive(*self.mPreset))
    {
        Game_ShowText("...");
    }
    else
    {
        Game_ShowDialogue("generic_npc", "one", d.mDialogue);
        d.mTalked = true;
        Preset_SetAlive(*self.mPreset, false);
    }
    return true;
}

const EntityClass kSnowman = {
    "Snowman", SnowmanUpdate, nullptr, SnowmanCollided, SnowmanInteract, nullptr, nullptr, nullptr,
    0,         GROUP_PLAYER,  ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_Snowman(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSnowman, preset, "blue_npcs", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    SnowmanData& d = e->Data<SnowmanData>();
    d = SnowmanData();
    d.mDialogue = 2 * preset.mFrame;
    d.mIdleTimer = 3;
    Ecs_Play(*e, Preset_Alive(preset) ? "normal" : "melted");
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_CellBody(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kCellBody, preset, "cell_bodies", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrame(*e, preset.mFrame);
    return e;
}

namespace
{
// A VolumeEvent a cutscene owns (CutsceneState's EntityEvent): it ticks after each step of the cutscene.
struct CutsceneVolume final
{
    float mTarget;
    float mSpeed;
    bool mActive;
    bool mReached;

    void Start(float target, float speed)
    {
        mActive = true;
        mSpeed = speed;
        SetTarget(target);
    }
    void SetTarget(float target)
    {
        mTarget = target;
        mReached = false;
    }
    void Tick()
    {
        if (mActive && !mReached)
        {
            fx volume = FxF(Game_SongVolume());
            mReached = MoveTo(volume, FxF(mTarget), FxF(mSpeed));
            Game_SetSongVolume(volume.mV);
        }
    }
};

// ---- WaterAnim (Gadget/WaterAnim.cs): the BG tiles take the BG2 water pattern, tile by tile along its channel ------
bool WaterInGrid(const WaterFlow& w, int x, int y)
{
    return x / 10 == w.mGridX && y / 10 == w.mGridY;
}

} // namespace

// DoWaterAnim's MoveNext: false once the channel ends.
bool WaterStep(WaterFlow& w, fx startX, fx startY)
{
    if (!w.mStarted)
    {
        w.mStarted = true;
        w.mX = FxFloor(startX) / 16;
        w.mY = FxFloor(startY) / 16;
        w.mGridX = w.mX / 10;
        w.mGridY = w.mY / 10;
        w.mCurrent = World_GetTile(1, w.mX, w.mY);
        const bool horiz = (w.mCurrent % 2) == 0;
        w.mDirX = horiz ? ((w.mX + 1) / 10 == w.mGridX ? 1 : -1) : 0;
        w.mDirY = horiz ? 0 : ((w.mY + 1) / 10 == w.mGridY ? 1 : -1);
    }
    World_ChangeTile(0, w.mX, w.mY, w.mCurrent);
    w.mX += w.mDirX;
    w.mY += w.mDirY;
    if (!WaterInGrid(w, w.mX, w.mY) || World_GetTile(1, w.mX, w.mY) == 0)
    {
        return false;
    }
    if (WaterInGrid(w, w.mX + w.mDirX, w.mY + w.mDirY) && World_GetTile(1, w.mX + w.mDirX, w.mY + w.mDirY) == 0)
    {
        int nextX = w.mDirY;
        int nextY = w.mDirX;
        if (World_GetTile(1, w.mX + nextX, w.mY + nextY) == 0)
        {
            nextX = -nextX;
            nextY = -nextY;
        }
        if (World_GetTile(1, w.mX + nextX, w.mY + nextY) != 0)
        {
            w.mDirX = nextX;
            w.mDirY = nextY;
            w.mCurrent -= w.mCurrent % 2;
            if (w.mDirY != 0)
            {
                w.mCurrent++;
            }
        }
    }
    return true;
}

namespace
{

// ---- Dam (Npc/Blue/Dam.cs, "Happy_NPC" 18): falls once enough puzzles are solved ------------------------------------
void DamUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (!Preset_Alive(*self.mPreset))
    {
        return;
    }
    const int needed = strcmp(gGame.mMapName, "BLUE") == 0 ? 3 : 1;
    if (gGame.mPuzzlesSolved >= needed)
    {
        Ecs_Play(self, "fall");
        Preset_SetAlive(*self.mPreset, false);
    }
}

void DamCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kDam = {
    "Dam", DamUpdate, nullptr, DamCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- HappyNPC (Npc/Happy/HappyNPC.cs, "Happy_NPC" 0-4): paces or runs along, and talks -----------------------------
struct HappyNpcData final
{
    fx mWalkTimer;
    const char* mDialogue;
    int mCtr;
    bool mRun; // NpcType.Run (else Pace)
    bool mTalking;
};

void HappyNpcUpdate(Entity& self)
{
    HappyNpcData& d = self.Data<HappyNpcData>();
    Transform& t = *self.mT;
    Ecs_BaseUpdate(self);
    d.mWalkTimer += kFxTick;
    if (d.mTalking)
    {
        t.mVelX = 0;
        if (!Game_DialogueFinished())
        {
            return;
        }
        d.mTalking = false;
    }
    if (d.mWalkTimer <= Fx(1))
    {
        return;
    }
    d.mWalkTimer = 0;
    if (!d.mRun)
    {
        if (Rng_Range(0, 2) == 0)
        {
            d.mCtr = d.mCtr == 0 ? 1 : 0;
            t.mVelX = d.mCtr == 1 ? Fx(20) : -Fx(20);
            Ecs_Play(self, d.mCtr == 1 ? "walk_r" : "walk_l");
        }
        else
        {
            t.mVelX = 0;
            // RandomIdle: (Facing)RNG.Next(0, 5), one past the last direction included; nothing is played
            self.mFacing = static_cast<uint8_t>(Rng_Range(0, 5));
        }
        return;
    }
    d.mCtr = d.mCtr == 0 ? 1 : 0;
    Ecs_Play(self, d.mCtr == 1 ? "walk_r" : "walk_l");
    t.mVelX = d.mCtr == 1 ? Fx(40) : -Fx(40);
}

bool HappyNpcInteract(Entity& self, Facing playerFacing)
{
    HappyNpcData& d = self.Data<HappyNpcData>();
    const Transform& t = *self.mT;
    Ecs_FaceTowards(self, t.mX - Fx(FacingX(playerFacing)), t.mY - Fx(FacingY(playerFacing)));
    Ecs_PlayFacing(self, "idle");
    Game_ShowDialogue("happy_npc", d.mDialogue);
    d.mTalking = true;
    return true;
}

void HappyNpcCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kHappyNpc = {
    "HappyNPC", HappyNpcUpdate, nullptr, HappyNpcCollided, HappyNpcInteract, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER,   ECF_MAP_COLLISION | ECF_INTERACTABLE,
};

// ---- HappyEventTrigger (Npc/Happy/HappyEventTrigger.cs, "NPC" generic 1): the Briar's voice as HAPPY turns ---------
struct HappyTriggerData final
{
    float mSoundTime; // red_cave_rise's play time, in game time (the C#'s deterministic runs)
    uint8_t mStep;
};

constexpr float kRedCaveRiseSeconds = 3.475f;

void HappyTriggerUpdate(Entity& self)
{
    HappyTriggerData& d = self.Data<HappyTriggerData>();
    Ecs_BaseUpdate(self);
    Entity& p = *World_Player();
    switch (d.mStep)
    {
    case 0:
        if (InGrid(p.mT->mX) > 80)
        {
            // its VolumeEvent(0, 0.9f)
            fx volume = FxF(Game_SongVolume());
            MoveTo(volume, 0, FxF(0.9f));
            Game_SetSongVolume(volume.mV);
            return;
        }
        Game_PlaySong("");
        Game_PlaySfx("red_cave_rise");
        Game_ShowDialogue("happy_npc", "briar");
        d.mSoundTime = 0;
        d.mStep = 1;
        break;
    case 1:
        d.mSoundTime += 1.0f / kFps;
        break;
    default:
        return;
    }
    if (!Game_DialogueFinished() || d.mSoundTime < kRedCaveRiseSeconds)
    {
        Screen_Shake(FxF(0.02f), FxSec(0.1f));
        Player_SetDontMove(true);
        return;
    }
    Player_SetDontMove(false);
    Event_Inc("HappyStarted");
    // (the flash's callback, at once: Map.ReloadSettings picks up the HappyStarted settings. Of those only the music
    // is carried over here: the hardlight darkness and the tile replacements are still to do)
    Screen_Flash(FxSec(1), FLASH_RED);
    Game_PlaySong("happy");
    d.mStep = 2;
}

const EntityClass kHappyTrigger = {
    "HappyEventTrigger", HappyTriggerUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- GoHappyBlocker (Gadget/GoHappyBlocker.cs, "NPC" generic 11): thorns in GO until BLUE is done -----------------
const EntityClass kGoHappyBlocker = {
    "GoHappyBlocker", nullptr, nullptr, DamCollided, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_PART_OF_MAP,
};

// ---- BlueBriar + BlueMitra (Npc/Blue/BlueBriar.cs): Mitra rides in and talks, then the Briar walks out of the wall --
struct BlueBriarData final
{
    fx mTopLeftX;
    fx mTopLeftY;
    Entity* mMitra;
    fx mOpacityTimer; // BlueMitra's timer
    WaterFlow mWater;
    fx mWaterX;
    fx mWaterY;
    fx mWaterT;
    CutsceneVolume mVolume;
    uint8_t mState;     // State()
    uint8_t mCutStep;   // Entrance()
    uint8_t mMitraStep; // BlueMitra.Event()
    bool mHappy;        // HappyBriar (Npc/Happy/HappyBriar.cs): no Mitra; walks off to the right
    bool mWaterActive;
    bool mWaterWaiting;
};

BlueBriarData* gBlueBriar = nullptr; // (the Mitra's cutscene reads its owner's data)

bool BlueMitraEvent(Entity& mitra)
{
    BlueBriarData& d = *gBlueBriar;
    Transform& t = *mitra.mT;
    bool done = false;
    switch (d.mMitraStep)
    {
    case 0:
        mitra.mV->mVisible = true;
        d.mVolume.Start(0, 2.4f);
        d.mMitraStep = 1;
        break;
    case 1:
        if (!d.mVolume.mReached)
        {
            break;
        }
        d.mOpacityTimer = 0;
        d.mMitraStep = 2;
        [[fallthrough]];
    case 2:
        if (!MoveTo(d.mOpacityTimer, FxF(0.5f), FxF(0.6f)))
        {
            break;
        }
        Game_PlaySong("mitra", 0.5f);
        Game_ShowCutsceneDialogue("mitra", "one");
        d.mMitraStep = 3;
        break;
    case 3:
        d.mVolume.mSpeed = 0.6f;
        d.mVolume.SetTarget(1);
        t.mVelX = -Fx(70);
        d.mMitraStep = 4;
        [[fallthrough]];
    case 4:
        if (t.mX >= d.mTopLeftX + Fx(75))
        {
            break;
        }
        gGame.mPuzzlesSolved += 3;
        t.mVelX = 0;
        Ecs_Play(mitra, "idle");
        FlipForLeft(mitra);
        Game_ShowCutsceneDialogue("mitra", "one");
        d.mMitraStep = 5;
        break;
    case 5:
        Game_ShowCutsceneDialogue("mitra", "one");
        d.mMitraStep = 6;
        break;
    case 6:
        Ecs_Play(mitra, "bike_r");
        FlipForLeft(mitra);
        t.mVelX = Fx(40);
        d.mMitraStep = 7;
        [[fallthrough]];
    default:
        if (t.mX < d.mTopLeftX + Fx(160))
        {
            break;
        }
        mitra.mExists = false;
        done = true;
        break;
    }
    d.mVolume.Tick();
    return done;
}

// BlueBriar.Entrance
bool BlueBriarEntrance(Entity& self)
{
    BlueBriarData& d = self.Data<BlueBriarData>();
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    bool done = false;
    constexpr float kAppearanceVel = 0.24f;
    switch (d.mCutStep)
    {
    case 0:
        d.mVolume.Start(0, 0.6f);
        d.mCutStep = 1;
        break;
    case 1:
        if (!d.mVolume.mReached)
        {
            break;
        }
        Ecs_Play(self, "walk_d@2");
        v.mOpacity = 0;
        v.mVisible = true;
        Game_PlaySong("");
        d.mCutStep = 2;
        [[fallthrough]];
    case 2:
        if (!MoveTo(v.mOpacity, FxF(0.4f), FxF(kAppearanceVel)))
        {
            break;
        }
        t.mVelY = Fx(20);
        Game_PlaySong("go", 0.4f);
        d.mVolume.SetTarget(1);
        d.mVolume.mSpeed = kAppearanceVel;
        // water_anim = OnceEvery(DoWaterAnim(Position), 0.3f): it runs in the Briar's update
        d.mWater = WaterFlow();
        d.mWaterX = t.mX;
        d.mWaterY = t.mY;
        d.mWaterActive = true;
        d.mWaterWaiting = false;
        d.mCutStep = 3;
        [[fallthrough]];
    case 3:
        if (InGrid(t.mY) < 44)
        {
            MoveTo(v.mOpacity, kFxOne, FxF(kAppearanceVel));
            break;
        }
        v.mOpacity = kFxOne;
        t.mVelX = d.mHappy ? Fx(20) : -Fx(20);
        t.mVelY = 0;
        Ecs_Play(self, d.mHappy ? "walk_r@2" : "walk_l@2");
        d.mCutStep = 4;
        [[fallthrough]];
    default:
        // (wraps around when off screen)
        if (d.mHappy ? InGrid(t.mX) > 20 : InGrid(t.mX) >= (d.mTopLeftX - Fx(16)).mV)
        {
            break;
        }
        self.mExists = false;
        Preset_SetAlive(*self.mPreset, false);
        Event_Inc(d.mHappy ? "HappyDone" : "BlueDone");
        done = true;
        break;
    }
    d.mVolume.Tick();
    return done;
}

void BlueBriarUpdate(Entity& self)
{
    BlueBriarData& d = self.Data<BlueBriarData>();
    gBlueBriar = &d;
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    switch (d.mState)
    {
    case 0:
        if (d.mHappy)
        {
            if (gGame.mPuzzlesSolved != 1)
            {
                break;
            }
            d.mCutStep = 0;
            Game_StartCutscene(self, BlueBriarEntrance);
            d.mState = 2;
            break;
        }
        if (p.mT->mX >= d.mTopLeftX + Fx(76) || p.mT->mY >= d.mTopLeftY + Fx(47) || Player_State() != PS_GROUND)
        {
            break;
        }
        d.mMitraStep = 0;
        Game_StartCutscene(*d.mMitra, BlueMitraEvent);
        d.mState = 1;
        [[fallthrough]];
    case 1:
        if (d.mMitra->mExists)
        {
            break;
        }
        d.mCutStep = 0;
        Game_StartCutscene(self, BlueBriarEntrance);
        d.mState = 2;
        break;
    default:
        break;
    }
    // water_anim?.MoveNext()
    if (d.mWaterActive)
    {
        if (d.mWaterWaiting)
        {
            if (!MoveTo(d.mWaterT, FxF(0.3f), kFxOne))
            {
                return;
            }
            d.mWaterWaiting = false;
        }
        if (WaterStep(d.mWater, d.mWaterX, d.mWaterY))
        {
            d.mWaterWaiting = true;
            d.mWaterT = 0;
        }
        else
        {
            d.mWaterActive = false;
        }
    }
}

void BlueBriarDestroy(Entity& self)
{
    if (gBlueBriar == &self.Data<BlueBriarData>())
    {
        gBlueBriar = nullptr;
    }
}

const EntityClass kBlueBriar = {
    "BlueBriar", BlueBriarUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, BlueBriarDestroy, 0, 0, 0,
};

const EntityClass kBlueMitra = {
    "BlueMitra", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, GROUP_BLUE_MITRA, 0, 0,
};

// ---- PostBlue + PostBlueSage (Npc/Go/PostBlue.cs, "Mitra" in GO): after BLUE, Mitra rides in and the Sage fires ----
// The cutscene's own entities (PlayerStandin, SageBullet, its Explosion) are plain entities here, under names the C#
// never lists in the play state.
const EntityClass kPlayerStandin = {
    "PlayerStandin", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kSageBullet = {
    "SageBullet", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kCutsceneExplosion = {
    "CutsceneExplosion", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

struct PostBlueData final
{
    MitraData mMitra; // (first: the Mitra helpers read it)
    Entity* mSage;
    Entity* mStandin;
    Entity* mBullet;
    Entity* mExplosion;
    fx mTopLeftX;
    fx mTopLeftY;
    uint8_t mStep;
    uint8_t mTalks;
    bool mStarted;
};

Entity* MakeBike(Entity& owner, const MapEntity* preset, fx x, fx y)
{
    Entity* b = Ecs_Create(kBike, x, y, PART_VISUAL, preset, &owner);
    if (b)
    {
        Ecs_SetSheet(*b, "bike", 20, 20);
        b->mB->mW = 16;
        b->mB->mH = 10;
        b->mV->mOffX = Fx(2);
        b->mV->mOffY = Fx(11);
        b->mB->mImmovable = true;
        Ecs_Play(*b, "r");
    }
    return b;
}

// The cutscene keeps the player in the room while they may move.
void PostBlueClampPlayer(const PostBlueData& d)
{
    Transform& p = *World_Player()->mT;
    const fx lo = d.mTopLeftX + Fx(16);
    const fx hi = d.mTopLeftX + Fx(9 * 16);
    p.mX = p.mX < lo ? lo : (p.mX > hi ? hi : p.mX);
    // (the C# clamps Y with the room's X as well)
    p.mY = p.mY < lo ? lo : (p.mY > hi ? hi : p.mY);
}

Entity* PostBlueExplosion(const Entity& bullet)
{
    Game_PlaySfx("hit_wall");
    Entity* e = Ecs_Create(kCutsceneExplosion, bullet.mT->mX - Fx(4), bullet.mT->mY - Fx(4), PART_VISUAL);
    if (e)
    {
        Ecs_SetSolid(*e, false);
        Ecs_SetSheet(*e, "enemy_explode_2");
        e->mV->mLayer = LAYER_BG_ENTITIES;
        Ecs_Play(*e, "explode");
    }
    return e;
}

// PostBlue.Cutscene
bool PostBlueCutscene(Entity& self)
{
    PostBlueData& d = self.Data<PostBlueData>();
    Transform& t = *self.mT;
    Entity& p = *World_Player();
    Entity& bike = *d.mMitra.mBike;
    Entity& sage = *d.mSage;
    switch (d.mStep)
    {
    case 0:
        d.mStandin = Ecs_Create(kPlayerStandin, p.mT->mX, p.mT->mY, PART_VISUAL);
        if (d.mStandin)
        {
            Entity& s = *d.mStandin;
            Ecs_SetSheet(s, "young_player", 16, 16);
            Ecs_Play(s, Ecs_AnimName(p));
            s.mB->mW = p.mB->mW;
            s.mB->mH = p.mB->mH;
            s.mV->mOffX = p.mV->mOffX;
            s.mV->mOffY = p.mV->mOffY;
            Ecs_SetSolid(s, false);
        }
        p.mV->mVisible = false;
        d.mStep = 1;
        return false;
    case 1:
    {
        Transform& st = *d.mStandin->mT;
        if (st.mX < d.mTopLeftX + Fx(7 * 16))
        {
            st.mVelX = Fx(60);
            Ecs_Play(*d.mStandin, "walk_r");
            return false;
        }
        st.mVelX = st.mVelY = 0;
        st.mX = d.mTopLeftX + Fx(7 * 16);
        d.mStep = 2;
        [[fallthrough]];
    }
    case 2:
    {
        Transform& st = *d.mStandin->mT;
        if (st.mY < d.mTopLeftY + Fx(7 * 16))
        {
            st.mVelY = Fx(60);
            Ecs_Play(*d.mStandin, "walk_d");
            return false;
        }
        st.mVelX = st.mVelY = 0;
        st.mY = d.mTopLeftY + Fx(7 * 16);
        p.mT->mX = st.mX;
        p.mT->mY = st.mY;
        p.mV->mVisible = true;
        Player_SetFacing(FACE_RIGHT);
        d.mStandin->mExists = false;
        d.mStandin = nullptr;
        t.mX = d.mTopLeftX + Fx(160);
        t.mY = d.mTopLeftY + Fx(5 * 16);
        t.mVelX = -Fx(45);
        t.mVelY = 0;
        self.mV->mVisible = true;
        d.mStep = 3;
        [[fallthrough]];
    }
    case 3:
        if (t.mX > p.mT->mX - Fx(24))
        {
            return false;
        }
        t.mVelX = 0;
        t.mVelY = Fx(45);
        d.mStep = 4;
        [[fallthrough]];
    case 4:
        if (t.mY < p.mT->mY - Fx(4))
        {
            return false;
        }
        MitraOnBike(self, false);
        bike.mExists = true;
        bike.mT->mX = p.mT->mX - Fx(18);
        bike.mT->mY = p.mT->mY + Fx(10);
        t.mVelX = 0;
        t.mVelY = Fx(20);
        d.mStep = 5;
        [[fallthrough]];
    case 5:
    {
        const Rect ph = Ecs_Hitbox(p);
        if (t.mY <= Fx(ph.mY + ph.mH))
        {
            return false;
        }
        t.mVelX = Fx(20);
        t.mVelY = 0;
        d.mStep = 6;
        [[fallthrough]];
    }
    case 6:
        if (t.mX <= p.mT->mX)
        {
            return false;
        }
        Player_SetFacing(FACE_DOWN);
        self.mFacing = FACE_UP;
        t.mVelX = t.mVelY = 0;
        sage.mV->mVisible = true;
        sage.mT->mX = d.mTopLeftX + Fx(4 * 16 + 3);
        sage.mT->mY = d.mTopLeftY + Fx(160 + 3);
        sage.mT->mVelX = 0;
        sage.mT->mVelY = -Fx(20);
        d.mStep = 7;
        [[fallthrough]];
    case 7:
        if (sage.mT->mY >= d.mTopLeftY + Fx(8 * 16 - 5))
        {
            return false;
        }
        sage.mT->mVelX = -Fx(20);
        sage.mT->mVelY = 0;
        d.mStep = 8;
        [[fallthrough]];
    case 8:
        if (sage.mT->mX >= d.mTopLeftX + Fx(2 * 16 + 11))
        {
            return false;
        }
        sage.mT->mVelX = sage.mT->mVelY = 0;
        sage.mFacing = FACE_RIGHT;
        Player_SetFacing(FACE_LEFT);
        self.mFacing = FACE_LEFT;
        Dialog_SetSceneProgress("sage", "one", 0);
        d.mTalks = 0;
        d.mStep = 9;
        [[fallthrough]];
    case 9:
        if (d.mTalks < 7)
        {
            d.mTalks++;
            Game_ShowCutsceneDialogue("sage", "one");
            return false;
        }
        Game_PlaySfx("sun_guy_charge");
        d.mBullet = Ecs_Create(kSageBullet, sage.mT->mX + Fx(sage.mB->mW), sage.mT->mY, PART_VISUAL);
        if (d.mBullet)
        {
            Entity& b = *d.mBullet;
            Ecs_SetSheet(b, "sage_attacks", 16, 16);
            b.mB->mW = b.mB->mH = 16;
            Ecs_SetSolid(b, false);
            Ecs_Play(b, "a");
            Ecs_Flicker(b, FxSec(2.5f));
            b.mKeep = true;
        }
        d.mStep = 10;
        return false;
    case 10:
        if (d.mBullet->mV->mFlickering)
        {
            PostBlueClampPlayer(d);
            Player_SetDontMove(false);
            Player_SetActionsDisabled(false);
            return false;
        }
        Game_PlaySfx("laser-pew");
        d.mBullet->mT->mVelX = Fx(10);
        d.mStep = 11;
        [[fallthrough]];
    case 11:
    {
        PostBlueClampPlayer(d);
        Entity& b = *d.mBullet;
        const Rect bh = Ecs_Hitbox(b);
        const bool hitPlayer = Ecs_Intersects(bh, Ecs_Hitbox(p));
        if (hitPlayer || Ecs_Intersects(bh, Player_BroomHitbox()))
        {
            // (the flash eases in at once: its callback runs straight away)
            Screen_Flash(FxSec(1), hitPlayer ? FLASH_RED : FLASH_WHITE);
            b.mV->mVisible = false;
            if (hitPlayer)
            {
                Player_Slump();
            }
            d.mExplosion = PostBlueExplosion(b);
            d.mStep = 12;
            return false;
        }
        if (b.mT->mX > bike.mT->mX)
        {
            Screen_Flash(FxSec(1), FLASH_RED);
            b.mV->mVisible = false;
            d.mExplosion = PostBlueExplosion(b);
            d.mStep = 20;
            return false;
        }
        Player_SetDontMove(false);
        Player_SetActionsDisabled(false);
        return false;
    }
    case 12:
        Game_ShowCutsceneDialogue("sage", "hit", 0);
        d.mStep = 13;
        return false;
    case 13:
        Game_ShowCutsceneDialogue("sage", "hit", 1);
        d.mStep = 30;
        return false;
    case 20:
        Game_ShowDialogue("sage", "hit", 2);
        t.mVelX = -Fx(20);
        t.mVelY = 0;
        d.mStep = 21;
        [[fallthrough]];
    case 21:
        if (t.mX > bike.mT->mX)
        {
            return false;
        }
        t.mVelX = t.mVelY = 0;
        self.mFacing = FACE_UP;
        d.mStep = 22;
        [[fallthrough]];
    case 22:
        if (!Game_DialogueFinished())
        {
            return false;
        }
        Game_ShowCutsceneDialogue("sage", "hit", 3);
        d.mStep = 23;
        return false;
    case 23:
        self.mFacing = FACE_LEFT;
        Game_ShowCutsceneDialogue("sage", "hit", 4);
        d.mStep = 30;
        return false;
    case 30:
        Game_ShowCutsceneDialogue("sage", "hit", 5);
        d.mStep = 31;
        return false;
    default:
        Preset_SetActivated(*self.mPreset, true);
        d.mMitra.mFacePlayer = true;
        Ecs_SetSolid(self, true);
        Ecs_SetSolid(bike, true);
        // (the cutscene's entities go with it)
        if (d.mBullet)
        {
            d.mBullet->mKeep = false;
            d.mBullet->mExists = false;
            d.mBullet = nullptr;
        }
        if (d.mExplosion)
        {
            d.mExplosion->mExists = false;
            d.mExplosion = nullptr;
        }
        return true;
    }
}

void PostBlueUpdate(Entity& self)
{
    PostBlueData& d = self.Data<PostBlueData>();
    Ecs_BaseUpdate(self);
    MitraFacePlayerPose(self);
    const Entity& p = *World_Player();
    if (!Preset_Activated(*self.mPreset) && !d.mStarted && !World_InTransition() &&
        p.mT->mX < d.mTopLeftX + Fx(7 * 16) && p.mT->mY < d.mTopLeftY + Fx(7 * 16))
    {
        d.mStep = 0;
        Game_StartCutscene(self, PostBlueCutscene);
        d.mStarted = true;
    }
}

bool PostBlueInteract(Entity& self, Facing)
{
    if (!self.Data<PostBlueData>().mMitra.mFacePlayer)
    {
        return false;
    }
    if (Event_Get("HappyDone") > 0)
    {
        Game_ShowDialogue("sage", "posthappy_mitra");
    }
    else
    {
        Game_ShowDialogue("sage", "hit", 7);
    }
    return true;
}

void PostBlueDestroy(Entity& self)
{
    PostBlueData& d = self.Data<PostBlueData>();
    Entity** const subs[] = { &d.mMitra.mBike, &d.mSage, &d.mStandin, &d.mBullet, &d.mExplosion };
    for (Entity** sub : subs)
    {
        if (*sub)
        {
            Ecs_Free(**sub);
            *sub = nullptr;
        }
    }
}

const EntityClass kPostBlue = {
    "PostBlue",   PostBlueUpdate, nullptr, MitraCollided, PostBlueInteract, nullptr, nullptr, PostBlueDestroy,
    0,            GROUP_PLAYER,   ECF_MAP_COLLISION | ECF_INTERACTABLE,
};

// PostBlueSage: a SpriteSage that faces the player and is solid once the cutscene has run.
void PostBlueSageUpdate(Entity& self)
{
    SageData& d = self.Data<SageData>();
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    Transform& t = *self.mT;
    if (d.mFacePlayer)
    {
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
    }
    else if (t.mVelX != 0 || t.mVelY != 0)
    {
        Ecs_FaceTowards(self, t.mX + t.mVelX, t.mY + t.mVelY);
    }
    Ecs_PlayFacing(self, (t.mVelX == 0 && t.mVelY == 0) ? "idle" : "walk");
    FlipForLeft(self);
    d.mFacePlayer = Preset_Activated(*self.mPreset);
    Ecs_SetSolid(self, d.mFacePlayer);
}

bool PostBlueSageInteract(Entity& self, Facing)
{
    if (!self.Data<SageData>().mFacePlayer)
    {
        return false;
    }
    if (Event_Get("HappyDone") > 0)
    {
        Game_ShowDialogue("sage", "posthappy_sage");
    }
    else
    {
        Game_ShowDialogue("sage", "hit", 6);
    }
    return true;
}

const EntityClass kPostBlueSage = {
    "PostBlueSage", PostBlueSageUpdate, nullptr, SageCollided, PostBlueSageInteract, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,       ECF_INTERACTABLE,
};
} // namespace

Entity* Spawn_Dam(const MapEntity& preset, Entity&)
{
    const bool blue = strcmp(gGame.mMapName, "BLUE") == 0;
    Entity* e = Spawn_Basic(kDam, preset, blue ? "blue_npcs" : "happy_npcs", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, blue ? 0 : 8);
    Ecs_Play(*e, "idle");
    e->mB->mImmovable = true;
    return e;
}

Entity* Spawn_WaterAnim(const MapEntity& preset, Entity&)
{
    if (Event_Get(Preset_TypeValue(preset)) != 0)
    {
        WaterFlow w = WaterFlow();
        while (WaterStep(w, Fx(preset.mX), Fx(preset.mY)))
        {
        }
    }
    return nullptr;
}

Entity* Spawn_BlueBriar(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBlueBriar, preset, "briar", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    BlueBriarData& d = e->Data<BlueBriarData>();
    d = BlueBriarData();
    d.mTopLeftX = Fx(preset.mX) - fx(InGrid(Fx(preset.mX)));
    d.mTopLeftY = Fx(preset.mY) - fx(InGrid(Fx(preset.mY)));
    Ecs_SetSolid(*e, false);
    Ecs_Play(*e, "idle_d");
    e->mV->mVisible = false;
    gBlueBriar = &d;
    if (strcmp(gGame.mMapName, "HAPPY") == 0)
    {
        d.mHappy = true;
        return e;
    }
    Entity* m = Ecs_Create(kBlueMitra, d.mTopLeftX + Fx(160), d.mTopLeftY + Fx(33), PART_VISUAL, nullptr, e);
    if (!m)
    {
        return nullptr;
    }
    Ecs_SetSheet(*m, "mitra_bike", 20, 20);
    m->mB->mW = m->mB->mH = 20;
    Ecs_SetSolid(*m, false);
    m->mV->mVisible = false;
    Ecs_Play(*m, "bike_l");
    FlipForLeft(*m);
    m->mKeep = true; // (the Briar checks it after it rides off)
    d.mMitra = m;
    gBlueBriar = &d;
    return e;
}

Entity* Spawn_PostBlue(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kPostBlue, preset, "mitra");
    if (!e)
    {
        return nullptr;
    }
    const bool activated = Preset_Activated(preset);
    PostBlueData& d = e->Data<PostBlueData>();
    d = PostBlueData();
    d.mTopLeftX = Fx(preset.mX) - fx(InGrid(Fx(preset.mX)));
    d.mTopLeftY = Fx(preset.mY) - fx(InGrid(Fx(preset.mY)));
    e->mB->mImmovable = true;
    d.mMitra.mFacePlayer = true;
    d.mMitra.mVolReached = true;
    MitraOnBike(*e, !activated);
    e->mT->mX = d.mTopLeftX + Fx(6 * 16 + 2);
    e->mT->mY = d.mTopLeftY + Fx(8 * 16);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    d.mMitra.mBike = MakeBike(*e, &preset, d.mTopLeftX + Fx(6 * 16 + 2), d.mTopLeftY + Fx(7 * 16));
    if (d.mMitra.mBike)
    {
        d.mMitra.mBike->mExists = false;
    }
    // PostBlueSage (a sub-entity: there even when the Mitra isn't)
    d.mSage = Ecs_Create(kPostBlueSage, d.mTopLeftX + Fx(43), d.mTopLeftY + Fx(7 * 16 + 5), PART_VISUAL | PART_BRAIN,
                         &preset, e);
    if (!d.mMitra.mBike || !d.mSage)
    {
        return nullptr;
    }
    Entity& sage = *d.mSage;
    Ecs_SetSheet(sage, "sage", 16, 16);
    sage.mB->mW = sage.mB->mH = 10;
    sage.mV->mOffX = sage.mV->mOffY = Fx(3);
    sage.mB->mImmovable = true;
    SageData& sd = sage.Data<SageData>();
    sd = SageData();
    sd.mFacePlayer = activated;
    Ecs_Play(sage, "idle_d");
    if (!activated)
    {
        sage.mV->mVisible = false;
        Ecs_SetSolid(sage, false);
    }
    if (Event_Get("BlueDone") == 0)
    {
        e->mExists = false;
        return e;
    }
    if (activated)
    {
        d.mMitra.mBike->mExists = true;
    }
    else
    {
        e->mV->mVisible = false;
        d.mMitra.mFacePlayer = false;
        Ecs_SetSolid(*e, false);
        Ecs_SetSolid(*d.mMitra.mBike, false);
    }
    Ecs_PlayFacing(*e, "idle");
    return e;
}

Entity* Spawn_GoHappyBlocker(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kGoHappyBlocker, preset, "briar_ground_thorn", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "a");
    if (Event_Get("BlueDone") == 1)
    {
        e->mExists = false;
        return e;
    }
    e->mB->mImmovable = true;
    e->mB->mW = e->mB->mH = 20;
    e->mT->mY += Fx(2);
    e->mT->mLastY = e->mT->mY;
    return e;
}

Entity* Spawn_HappyNpc(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kHappyNpc, preset, "happy_npcs", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    static const int kBase[] = { 9, 9, 0, 9, 0 };
    static const char* const kDialogue[] = { "hot", "dump", "gold", "drink", "beautiful" };
    const int frame = preset.mFrame >= 0 && preset.mFrame < 5 ? preset.mFrame : 0;
    Ecs_SetFrameBase(*e, kBase[frame]);
    Ecs_Play(*e, "idle_d");
    e->mB->mImmovable = true;
    HappyNpcData& d = e->Data<HappyNpcData>();
    d = HappyNpcData();
    d.mDialogue = kDialogue[frame];
    d.mRun = frame >= 1 && frame <= 3;
    return e;
}

// ---- DeathPlace (Npc/DeathPlace.cs, "NPC" generic 9): in the death room, walking down out of it goes to the title --
namespace
{
struct DeathPlaceData final
{
    int mBottom; // the top of the screen below the one it was made on (MapUtilities.GetRoomUpperLeftPos)
};

void DeathPlacePostUpdate(Entity& self)
{
    if (Player_Entity()->mT->mY >= Fx(self.Data<DeathPlaceData>().mBottom - 14))
    {
        Game_RequestTitle();
        self.mExists = false;
    }
}

const EntityClass kDeathPlace = {
    "DeathPlace", nullptr, DeathPlacePostUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};
} // namespace

Entity* Spawn_DeathPlace(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kDeathPlace, preset, nullptr);
    if (!e)
    {
        return nullptr;
    }
    e->Data<DeathPlaceData>().mBottom = (gGame.mGridY + 1) * 160;
    e->mExists = gGame.mInDeathRoom;
    return e;
}

Entity* Spawn_HappyEventTrigger(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kHappyTrigger, preset, nullptr);
    if (!e)
    {
        return nullptr;
    }
    e->Data<HappyTriggerData>() = HappyTriggerData();
    if (Event_Get("HappyStarted") != 0)
    {
        e->mExists = false;
    }
    return e;
}

// ---- SageSentinel + EndingSage (Npc/Go/EndingSage.cs, "Sage" in GO): Briar's farewell, then the credits ----------
// The sentinel sits invisible at (0, 0); its EndingSage is a UIEntity (here a plain entity drawn as UI, under a name the
// C# never lists in the play state) and its sub-entity the walking Briar (a plain "Entity").
namespace
{
const EntityClass kEndingSage = {
    "EndingSage", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kEndingBriar = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

struct SageSentinelData final
{
    Entity* mSage;
    Entity* mBriar;
    Entity* mStandin;
    fx mTopLeftX;
    fx mTopLeftY;
    fx mTimer;
    float mWhite; // gameScreenFade.alpha
    float mBlack; // black_overlay.alpha
    uint8_t mStep;
    bool mStarted;
    bool mVolumeReached; // the VolumeEvent's ReachedTarget
};

// VolumeEvent(0, 0.18f).Update
void EndingVolumeStep(SageSentinelData& d)
{
    if (!d.mVolumeReached)
    {
        fx volume = FxF(Game_SongVolume());
        d.mVolumeReached = MoveTo(volume, 0, FxF(0.18f));
        Game_SetSongVolume(volume.mV);
    }
}

// EndingSage.EndingCutscene
bool EndingCutscene(Entity& self)
{
    SageSentinelData& d = self.Data<SageSentinelData>();
    Entity& p = *World_Player();
    Transform& pt = *p.mT;
    Entity& briar = *d.mBriar;
    Entity& sage = *d.mSage;
    const fx tlx = d.mTopLeftX;
    const fx tly = d.mTopLeftY;
    switch (d.mStep)
    {
    case 0:
        if (pt.mY > tly + Fx(140) || p.mV->mYPush < Fx(14))
        {
            Player_SetDontMove(false);
            pt.mX = pt.mX < tlx + Fx(64) ? tlx + Fx(64) : (pt.mX > tlx + Fx(88) ? tlx + Fx(88) : pt.mX);
            pt.mY = pt.mY < tly + Fx(16 * 6) ? tly + Fx(16 * 6) : pt.mY;
            return false;
        }
        p.mV->mYPush = Fx(14);
        p.mExists = false;
        d.mStandin = Ecs_Create(kPlayerStandin, pt.mX, pt.mY, PART_VISUAL);
        if (d.mStandin)
        {
            Entity& s = *d.mStandin;
            Ecs_SetSheet(s, "young_player", 16, 16);
            Ecs_Play(s, Ecs_AnimName(p));
            s.mB->mW = p.mB->mW;
            s.mB->mH = p.mB->mH;
            s.mV->mOffX = p.mV->mOffX;
            s.mV->mOffY = p.mV->mOffY;
            s.mV->mYPush = p.mV->mYPush;
            Ecs_SetSolid(s, false);
        }
        d.mStep = 1;
        return false;
    case 1:
        Game_ShowCutsceneDialogue("briar", "final");
        d.mStep = 2;
        return false;
    case 2:
        briar.mT->mY = tly - Fx(16);
        briar.mT->mVelY = Fx(25);
        briar.mV->mYPush = Fx(14);
        Ecs_Play(briar, "walk_d");
        Ecs_Play(*d.mStandin, "idle_u");
        d.mStep = 3;
        [[fallthrough]];
    case 3:
    {
        const Transform& st = *d.mStandin->mT;
        const float dx = (briar.mT->mX - st.mX).mV;
        const float dy = (briar.mT->mY - st.mY).mV;
        if (sqrtf(dx * dx + dy * dy) > 7)
        {
            MoveTo(briar.mT->mX, st.mX, Fx(10));
            return false;
        }
        briar.mT->mVelX = briar.mT->mVelY = 0;
        d.mStep = 4;
        [[fallthrough]];
    }
    case 4:
        if (!MoveTo(briar.mV->mYPush, Fx(6), FxF(1 / 0.3f)))
        {
            d.mStandin->mV->mYPush = p.mV->mYPush = briar.mV->mYPush;
            return false;
        }
        Game_ShowCutsceneDialogue("briar", "final");
        d.mStep = 5;
        return false;
    case 5:
        Ecs_Play(*d.mStandin, "walk_r");
        d.mStep = 6;
        [[fallthrough]];
    case 6:
        if (!MoveTo(d.mStandin->mT->mX, tlx + Fx(85), Fx(15)))
        {
            pt.mX = d.mStandin->mT->mX;
            pt.mY = d.mStandin->mT->mY;
            return false;
        }
        Ecs_Play(*d.mStandin, "walk_l");
        d.mStep = 7;
        [[fallthrough]];
    case 7:
        if (!MoveTo(d.mStandin->mT->mX, tlx + Fx(65), Fx(15)))
        {
            pt.mX = d.mStandin->mT->mX;
            pt.mY = d.mStandin->mT->mY;
            return false;
        }
        Ecs_Play(*d.mStandin, "idle_u");
        Game_ShowCutsceneDialogue("briar", "final");
        d.mStep = 8;
        return false;
    case 8:
        Ecs_Play(briar, "walk_u");
        briar.mT->mVelY = -Fx(25);
        d.mStep = 9;
        [[fallthrough]];
    case 9:
        if (briar.mT->mY > tly - Fx(16))
        {
            return false;
        }
        // (UI space: x 160, InGrid(y) + 20 under the header)
        sage.mT->mX = tlx + Fx(160);
        sage.mT->mY = tly + fx(InGrid(d.mStandin->mT->mY));
        sage.mV->mVisible = true;
        Ecs_Play(sage, "walk_l");
        FlipForLeft(sage);
        sage.mT->mVelX = -Fx(24);
        d.mStep = 10;
        [[fallthrough]];
    case 10:
        if (sage.mT->mX > tlx + Fx(128))
        {
            return false;
        }
        Ecs_Play(*d.mStandin, "idle_r");
        Ecs_Play(sage, "idle_l");
        FlipForLeft(sage);
        sage.mT->mVelX = sage.mT->mVelY = 0;
        Game_ShowCutsceneDialogue("briar", "final");
        d.mStep = 11;
        return false;
    case 11:
        pt.mX = d.mStandin->mT->mX;
        pt.mY = d.mStandin->mT->mY;
        Player_SetFacing(FACE_RIGHT);
        p.mExists = true;
        d.mStandin->mExists = false;
        d.mStep = 12;
        [[fallthrough]];
    case 12:
        if (pt.mY > tly + Fx(8))
        {
            Player_SetDontMove(false);
            p.mV->mYPush = Fx(6);
            pt.mX = pt.mX < tlx + Fx(64) ? tlx + Fx(64) : (pt.mX > tlx + Fx(88) ? tlx + Fx(88) : pt.mX);
            if (pt.mY < tly + Fx(80))
            {
                MoveTo(p.mV->mOpacity, 0, FxF(0.3f));
            }
            return false;
        }
        d.mStep = 13;
        [[fallthrough]];
    case 13:
        if (!MoveTo(p.mV->mOpacity, 0, FxF(0.3f)))
        {
            return false;
        }
        p.mExists = false;
        d.mTimer = 0;
        d.mStep = 14;
        [[fallthrough]];
    case 14:
        if (!MoveTo(d.mTimer, Fx(1), Fx(1)))
        {
            return false;
        }
        Screen_SetWave(true);
        Screen_SetFadeWhite(true);
        Screen_SetFadeSparesUi(true); // (the sage is UI: over the game screen's fade)
        d.mStep = 15;
        [[fallthrough]];
    case 15:
        if (d.mWhite < 1)
        {
            // FadeEffect.ChangeAlpha(0.12f)
            d.mWhite = d.mWhite + 0.12f * kDeltaTime;
            d.mWhite = d.mWhite < 0 ? 0 : (d.mWhite > 1 ? 1 : d.mWhite);
            Screen_SetFade(fx(d.mWhite));
            return false;
        }
        Ecs_Play(sage, "walk_r");
        FlipForLeft(sage);
        sage.mT->mVelX = Fx(12);
        d.mStep = 16;
        [[fallthrough]];
    case 16:
        if (d.mBlack < 1)
        {
            d.mBlack = d.mBlack + 0.3f * kDeltaTime;
            d.mBlack = d.mBlack < 0 ? 0 : (d.mBlack > 1 ? 1 : d.mBlack);
            Screen_SetEndingBlack(fx(d.mBlack));
            return false;
        }
        // (the VolumeEvent updates after the step that adds it)
        EndingVolumeStep(d);
        d.mStep = 17;
        return false;
    case 17:
        if (!d.mVolumeReached)
        {
            EndingVolumeStep(d);
            return false;
        }
        Credits_Request();
        d.mStep = 18;
        return false;
    default:
        return false;
    }
}

void SageSentinelUpdate(Entity& self)
{
    SageSentinelData& d = self.Data<SageSentinelData>();
    Ecs_BaseUpdate(self);
    Transform& pt = *World_Player()->mT;
    pt.mY = pt.mY < d.mTopLeftY + Fx(144) ? pt.mY : d.mTopLeftY + Fx(144);
    if (!World_InTransition() && !d.mStarted)
    {
        d.mStarted = true;
        d.mStep = 0;
        Game_StartCutscene(self, EndingCutscene);
    }
}

void SageSentinelDestroy(Entity& self)
{
    SageSentinelData& d = self.Data<SageSentinelData>();
    Entity** const subs[] = { &d.mSage, &d.mBriar, &d.mStandin };
    for (Entity** sub : subs)
    {
        if (*sub)
        {
            Ecs_Free(**sub);
            *sub = nullptr;
        }
    }
}

const EntityClass kSageSentinel = {
    "SageSentinel", SageSentinelUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, SageSentinelDestroy, 0, 0, 0,
};
} // namespace

Entity* Spawn_SageSentinel(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kSageSentinel, 0, 0, PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    SageSentinelData& d = e->Data<SageSentinelData>();
    d = SageSentinelData();
    d.mTopLeftX = Fx(preset.mX) - fx(InGrid(Fx(preset.mX)));
    d.mTopLeftY = Fx(preset.mY) - fx(InGrid(Fx(preset.mY)));
    d.mSage = Ecs_Create(kEndingSage, 0, 0, PART_VISUAL, &preset, e);
    d.mBriar = Ecs_Create(kEndingBriar, d.mTopLeftX + Fx(4 * 16), d.mTopLeftY + Fx(60), PART_VISUAL, &preset, e);
    if (!d.mSage || !d.mBriar)
    {
        return nullptr;
    }
    Entity& sage = *d.mSage;
    Ecs_SetSheet(sage, "sage", 16, 16);
    Ecs_SetSolid(sage, false);
    sage.mV->mLayer = LAYER_UI;
    sage.mV->mVisible = false;
    Entity& briar = *d.mBriar;
    Ecs_SetSheet(briar, "briar", 16, 16);
    Ecs_SetSolid(briar, false);
    briar.mV->mYPush = Fx(8);
    Ecs_Play(briar, "walk_u");
    briar.mT->mVelY = -Fx(20);
    Player_SetGridEntranceAt(d.mTopLeftX + Fx(75), d.mTopLeftY + Fx(140));
    return e;
}

const char* Npcs_ProbeName(const Entity& e)
{
    if (e.mClass == &kBlueBriar && const_cast<Entity&>(e).Data<BlueBriarData>().mHappy)
    {
        return "HappyBriar";
    }
    if (e.mClass == &kMitra)
    {
        return const_cast<Entity&>(e).Data<MitraData>().mFields ? "MitraFields" : "MitraOverworld";
    }
    if (e.mClass != &kSage)
    {
        return nullptr;
    }
    switch (const_cast<Entity&>(e).Data<SageData>().mKind)
    {
    case SAGE_NEXUS:
        return "SageNexus";
    case SAGE_OVERWORLD:
        return "SageOverworld";
    case SAGE_REDCAVE:
        return "SageRedCave";
    case SAGE_CROWD:
        return "SageCrowd";
    default:
        return "SageBedroom";
    }
}
