// The game's state for the simulator's bot link and trace (game_probe.h).
#include "game_probe.h"

#include <stdarg.h>
#include <stdio.h>

#include "dialog.h"
#include "ecs.h"
#include "game_state.h"
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
    o.Add("\"player\":{\"x\":%d,\"y\":%d,\"facing\":%d,\"ladder\":%s,\"fallen\":%s,\"health\":%d},", p.mX, p.mY,
          p.mFacing, p.mLadder ? "true" : "false", p.mFallen ? "true" : "false", p.mHealth);
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
              "\"frame\":%d,\"enemy\":%s,\"preset\":%d}",
              first ? "" : ",", Str(e->mClass->mName), FxTrunc(e->mT->mX), FxTrunc(e->mT->mY), e->mB->mW, e->mB->mH,
              e->mExists ? "true" : "false", Ecs_Solid(*e) ? "true" : "false", Ecs_AnimName(*e), Ecs_Frame(*e),
              (e->mClass->mFlags & ECF_ENEMY) ? "true" : "false", e->mPreset ? e->mPreset->mFrame : -1);
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
