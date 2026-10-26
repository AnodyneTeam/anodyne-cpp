// The game's state for the simulator's bot link and trace (game_probe.h).
#include "game_probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dialog.h"
#include "ecs.h"
#include "entities.h"
#include "entity_classes.h"
#include "game_state.h"
#include "player.h"
#include "world.h"

namespace
{
struct Out final
{
    char* mBuf;
    int mCap;
    int mLen;

    void Add(const char* fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        if (mLen >= mCap - 1)
        {
            return;
        }
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(mBuf + mLen, static_cast<size_t>(mCap - mLen), fmt, args);
        va_end(args);
        mLen = n < 0 ? mLen : (mLen + n >= mCap ? mCap - 1 : mLen + n);
    }
};

const char* Str(const char* s)
{
    return s ? s : "";
}
} // namespace

// The C# class name of an entity: one port class can stand for several C# ones.
const char* ProbeName(const Entity& e)
{
    if (const char* name = Gadgets_ProbeName(e))
    {
        return name;
    }
    if (const char* name = Npcs_ProbeName(e))
    {
        return name;
    }
    return Str(e.mClass->mName);
}

extern "C" int GameProbe_Json(char* buf, int cap)
{
    if (cap <= 0)
    {
        return 0;
    }
    Out o = { buf, cap, 0 };
    buf[0] = '\0';
    WorldPlayerInfo p;
    World_GetPlayerInfo(p);
    o.Add("{\"mode\":\"%s\",\"map\":\"%s\",\"grid\":[%d,%d],\"transition\":%s,\"dialog\":%s,", Str(gGame.mModeName),
          Str(gGame.mMapName), p.mGridX, p.mGridY, p.mTransition ? "true" : "false", Dialog_IsOpen() ? "true" : "false");
    // GlobalState.Dialogue is set: a box that is not a cutscene's DialogueEvent
    o.Add("\"rng\":%u,", static_cast<unsigned>(Rng_Calls()));
    o.Add("\"globalDialog\":%s,", Dialog_IsOpen() && !Game_DialogueIsCutscene() ? "true" : "false");
    const Entity& pe = *Player_Entity();
    PlayerProbe pp;
    Player_GetProbe(pp);
    const Entity* dust = Player_BroomDust();
    o.Add("\"deathMenu\":%s,\"deaths\":%d,", p.mDeathMenu ? "true" : "false", p.mDeaths);
    o.Add("\"player\":{\"x\":%d,\"y\":%d,\"facing\":%d,\"ladder\":%s,\"fallen\":%s,\"health\":%d,", p.mX, p.mY,
          p.mFacing, p.mLadder ? "true" : "false", p.mFallen ? "true" : "false", p.mHealth);
    // Player.cs as the C# bot reads it: position and velocity (px/s) as raw 16.16 (x, y, vx, vy, additional vx, vy),
    // the hitbox size, the state, Facing order (LEFT RIGHT UP DOWN).
    o.Add("\"fx16\":[%d,%d,%d,%d,%d,%d],\"w\":%d,\"h\":%d,", FxRaw16(pe.mT->mX),
          FxRaw16(pe.mT->mY), FxRaw16(pe.mT->mVelX), FxRaw16(pe.mT->mVelY),
          FxRaw16(pp.mAddVelX), FxRaw16(pp.mAddVelY), pe.mB->mW, pe.mB->mH);
    o.Add("\"face\":%d,\"state\":%d,\"animState\":%d,\"exists\":%s,\"dontMove\":%s,\"conveyor\":%s,\"slow\":%s,"
          "\"touching\":%d,\"anim\":\"%s\",",
          pe.mFacing & 3, pp.mState, pp.mAnimState, pe.mExists ? "true" : "false", pp.mDontMove ? "true" : "false",
          pp.mOnConveyor ? "true" : "false", pp.mSlow ? "true" : "false", pe.mB->mTouching, Ecs_AnimName(pe));
    o.Add("\"broomOut\":%s,\"broomBox\":[%d,%d,%d,%d],\"dust\":%s,\"dustId\":%d,\"justReleased\":%s},", pp.mBroomOut ? "true" : "false",
          pp.mBroomX, pp.mBroomY, pp.mBroomW, pp.mBroomH, dust ? "true" : "false",
          dust ? static_cast<int>(dust->mIndex) : -1, pp.mJustReleasedDust ? "true" : "false");
    o.Add("\"maxHealth\":%d,\"keys\":%d,\"cards\":%d,\"broom\":%d,\"canJump\":%s,\"cutscene\":%s,",
          gGame.mMaxHealth, Inventory_MapKeys(), Inventory_CardCount(), gGame.mEquippedBroom,
          gGame.mCanJump ? "true" : "false", Game_InCutscene() ? "true" : "false");
    o.Add("\"checkpoint\":[%d,%d,%d],\"bosses\":%u,", gGame.mCheckpointMap, gGame.mCheckpointX, gGame.mCheckpointY,
          static_cast<unsigned>(gGame.mBossDefeated));
    // The map's presets that are dead (EntityPreset.Alive false: 1) or activated (2), as [x, y, frame, flags].
    o.Add("\"presets\":[");
    const MapEntityList& presets = Entities_Presets();
    bool firstPreset = true;
    for (uint32_t i = 0; i < presets.mCount; i++)
    {
        const MapEntity& pr = presets.mEntities[i];
        const int flags = (Preset_Alive(pr) ? 0 : 1) | (Preset_Activated(pr) ? 2 : 0);
        if (flags != 0)
        {
            o.Add("%s[%d,%d,%d,%d]", firstPreset ? "" : ",", pr.mX, pr.mY, pr.mFrame, flags);
            firstPreset = false;
        }
    }
    o.Add("],");
    o.Add("\"puzzles\":%d,\"killed\":%d,\"entities\":[", gGame.mPuzzlesSolved, gGame.mEnemiesKilled);
    bool first = true;
    for (const Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if ((e->mPlayerOwned && !(e->mClass->mFlags & ECF_PROBE_OWNED)) || e->mOld || e->mSpawned)
        {
            continue;
        }
        o.Add("%s{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"exists\":%s,\"solid\":%s,\"anim\":\"%s\","
              "\"frame\":%d,\"enemy\":%s,\"preset\":%d,",
              first ? "" : ",", ProbeName(*e), FxTrunc(e->mT->mX), FxTrunc(e->mT->mY), e->mB->mW, e->mB->mH,
              e->mExists ? "true" : "false", Ecs_Solid(*e) ? "true" : "false", Ecs_AnimName(*e), Ecs_Frame(*e),
              (e->mClass->mFlags & ECF_ENEMY) ? "true" : "false", e->mPreset ? e->mPreset->mFrame : -1);
        // For the C# bot: the pool slot, the parent's (-1: none), the preset's id, position and velocity as raw 16.16,
        // facing (Facing order).
        const Visual* v = e->mV;
        char fields[160];
        const int fieldsLen = Enemies_ProbeFields(*e, fields, sizeof(fields));
        if (const char* fsm = Enemies_ProbeFsm(*e))
        {
            o.Add("\"fsm\":\"%s\",", fsm);
        }
        o.Add("\"id\":%u,\"parent\":%d,\"gid\":%d,\"fx16\":[%d,%d,%d,%d],\"face\":%d,\"visible\":%s,\"flicker\":%s,"
              "\"opacity\":%d,\"f\":{%s}}",
              static_cast<unsigned>(e->mIndex), e->mParent ? static_cast<int>(e->mParent->mIndex) : -1,
              e->mPreset ? e->mPreset->mGlobalId : -1, FxRaw16(e->mT->mX),
              FxRaw16(e->mT->mY), FxRaw16(e->mT->mVelX), FxRaw16(e->mT->mVelY),
              e->mFacing & 3, v && v->mVisible ? "true" : "false", v && v->mFlickering ? "true" : "false",
              v ? FxRaw16(v->mOpacity) : 0, fieldsLen > 0 ? fields : "");
        first = false;
    }
    o.Add("]}");
    return o.mLen;
}

extern "C" int GameProbe_Collision(char* buf, int cap)
{
    if (cap <= 0)
    {
        return 0;
    }
    Out o = { buf, cap, 0 };
    buf[0] = '\0';
    int w = 0;
    int h = 0;
    World_GetCellsSize(w, h);
    o.Add("%d %d ", w, h);
    static const char kHex[] = "0123456789abcdef";
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w && o.mLen + 3 < cap; x++)
        {
            uint8_t allow = 0;
            uint8_t event = 0;
            World_GetCell(x, y, allow, event);
            buf[o.mLen++] = kHex[allow & 15];
            buf[o.mLen++] = kHex[event & 15];
        }
    }
    buf[o.mLen] = '\0';
    return o.mLen;
}

int GameProbe_Warp(const char* map, int x, int y)
{
    const int index = Game_MapIndex(map);
    if (index < 0)
    {
        return 0;
    }
    gGame.mHasBroom[BROOM_NORMAL] = true;
    gGame.mEquippedBroom = BROOM_NORMAL;
    World_RequestWarp(index, x, y, 2);
    return 1;
}

int GameProbe_SetCheckpoint(const char* map, int x, int y)
{
    const int index = Game_MapIndex(map);
    if (index < 0)
    {
        return 0;
    }
    gGame.mCheckpointMap = index;
    gGame.mCheckpointX = x;
    gGame.mCheckpointY = y;
    return 1;
}

void GameProbe_Heal(void)
{
    gGame.mCurHealth = gGame.mMaxHealth;
}

extern "C" int GameProbe_TraceHeader(char* buf, int cap)
{
    return snprintf(buf, static_cast<size_t>(cap), "frame\tmap\tx\ty\thp\tdialogue\tinv");
}

extern "C" int GameProbe_TraceRow(unsigned long long frame, char* buf, int cap)
{
    // BotRunner.Trace: no map or player outside the play state; positions truncated like (int)Position.X.
    WorldPlayerInfo p;
    World_GetPlayerInfo(p);
    // (the C#'s credits still show the play state's last map and player)
    const bool play = gGame.mModeName && (!strcmp(gGame.mModeName, "play") || !strcmp(gGame.mModeName, "credits"));
    return snprintf(buf, static_cast<size_t>(cap), "%llu\t%s\t%d\t%d\t%d\t%d\tb%dk%dc%d", frame,
                    play ? Str(gGame.mMapName) : "", play ? p.mX : 0, play ? p.mY : 0, gGame.mCurHealth,
                    Dialog_IsOpen() && !Game_DialogueIsCutscene() ? 1 : 0, gGame.mEquippedBroom != 0 ? 1 : 0, Inventory_MapKeys(),
                    Inventory_CardCount());
}

extern "C" void GameProbe_SetRng(unsigned draws)
{
    Rng_Seed(1234);
    Screen_ResetShake();
    for (unsigned i = 0; i < draws; i++)
    {
        Rng_Next();
    }
}
