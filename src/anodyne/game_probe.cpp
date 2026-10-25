// The game's state for the simulator's bot link and trace (game_probe.h).
#include "game_probe.h"

#include <stdarg.h>
#include <stdio.h>

#include "dialog.h"
#include "ecs.h"
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
    const Entity& pe = *Player_Entity();
    PlayerProbe pp;
    Player_GetProbe(pp);
    const Entity* dust = Player_BroomDust();
    o.Add("\"player\":{\"x\":%d,\"y\":%d,\"facing\":%d,\"ladder\":%s,\"fallen\":%s,\"health\":%d,", p.mX, p.mY,
          p.mFacing, p.mLadder ? "true" : "false", p.mFallen ? "true" : "false", p.mHealth);
    // Player.cs as the C# bot reads it: position and velocity (px/s) as raw 16.16 (x, y, vx, vy, additional vx, vy),
    // the hitbox size, the state, Facing order (LEFT RIGHT UP DOWN).
    o.Add("\"fx16\":[%d,%d,%d,%d,%d,%d],\"w\":%d,\"h\":%d,", static_cast<int>(pe.mT->mX),
          static_cast<int>(pe.mT->mY), static_cast<int>(pe.mT->mVelX), static_cast<int>(pe.mT->mVelY),
          static_cast<int>(pp.mAddVelX), static_cast<int>(pp.mAddVelY), pe.mB->mW, pe.mB->mH);
    o.Add("\"face\":%d,\"state\":%d,\"animState\":%d,\"exists\":%s,\"dontMove\":%s,\"conveyor\":%s,\"slow\":%s,"
          "\"touching\":%d,\"anim\":\"%s\",",
          pe.mFacing & 3, pp.mState, pp.mAnimState, pe.mExists ? "true" : "false", pp.mDontMove ? "true" : "false",
          pp.mOnConveyor ? "true" : "false", pp.mSlow ? "true" : "false", pe.mB->mTouching, Ecs_AnimName(pe));
    o.Add("\"broomOut\":%s,\"broomBox\":[%d,%d,%d,%d],\"dust\":%s},", pp.mBroomOut ? "true" : "false", pp.mBroomX,
          pp.mBroomY, pp.mBroomW, pp.mBroomH, dust ? "true" : "false");
    o.Add("\"maxHealth\":%d,\"keys\":%d,\"cards\":%d,\"broom\":%d,\"canJump\":%s,\"cutscene\":%s,",
          gGame.mMaxHealth, Inventory_MapKeys(), Inventory_CardCount(), gGame.mEquippedBroom,
          gGame.mCanJump ? "true" : "false", Game_InCutscene() ? "true" : "false");
    o.Add("\"checkpoint\":[%d,%d,%d],\"bosses\":%u,", gGame.mCheckpointMap, gGame.mCheckpointX, gGame.mCheckpointY,
          static_cast<unsigned>(gGame.mBossDefeated));
    o.Add("\"puzzles\":%d,\"killed\":%d,\"entities\":[", gGame.mPuzzlesSolved, gGame.mEnemiesKilled);
    bool first = true;
    for (const Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if (e->mPlayerOwned || e->mOld)
        {
            continue;
        }
        o.Add("%s{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"exists\":%s,\"solid\":%s,\"anim\":\"%s\","
              "\"frame\":%d,\"enemy\":%s,\"preset\":%d,",
              first ? "" : ",", Str(e->mClass->mName), FxTrunc(e->mT->mX), FxTrunc(e->mT->mY), e->mB->mW, e->mB->mH,
              e->mExists ? "true" : "false", Ecs_Solid(*e) ? "true" : "false", Ecs_AnimName(*e), Ecs_Frame(*e),
              (e->mClass->mFlags & ECF_ENEMY) ? "true" : "false", e->mPreset ? e->mPreset->mFrame : -1);
        // For the C# bot: the pool slot, the parent's (-1: none), the preset's id, position and velocity as raw 16.16,
        // facing (Facing order).
        const Visual* v = e->mV;
        o.Add("\"id\":%u,\"parent\":%d,\"gid\":%d,\"fx16\":[%d,%d,%d,%d],\"face\":%d,\"visible\":%s,\"flicker\":%s,"
              "\"opacity\":%d}",
              static_cast<unsigned>(e->mIndex), e->mParent ? static_cast<int>(e->mParent->mIndex) : -1,
              e->mPreset ? e->mPreset->mGlobalId : -1, static_cast<int>(e->mT->mX),
              static_cast<int>(e->mT->mY), static_cast<int>(e->mT->mVelX), static_cast<int>(e->mT->mVelY),
              e->mFacing & 3, v && v->mVisible ? "true" : "false", v && v->mFlickering ? "true" : "false",
              v ? static_cast<int>(v->mOpacity) : 0);
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

void GameProbe_Heal(void)
{
    gGame.mCurHealth = gGame.mMaxHealth;
}
