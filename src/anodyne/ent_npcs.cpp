// NPCs and decorations met before the first boss (AnodyneSharp Entities/Interactive, Decorations, Enemy/Etc).
#include "entity_classes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

void MitraUpdate(Entity& self)
{
    MitraData& d = self.Data<MitraData>();
    if (d.mFields)
    {
        Ecs_BaseUpdate(self);
        MitraFacePlayerPose(self);
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
    Entity* e = Spawn_Basic(kRock, preset, "note_rock");
    if (e)
    {
        e->mB->mImmovable = true;
        Ecs_SetFrame(*e, 0);
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
    Entity* light = Ecs_Create(kGlowLight, e->mT->mX + Fx(8), e->mT->mY + Fx(6), PART_VISUAL, nullptr, e);
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

const char* Npcs_ProbeName(const Entity& e)
{
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
