#include "pause.h"

#include <stdint.h>

#include "tonc.h"

#include "audio_manager.h"
#include "dialog.h"
#include "ecs.h"
#include "game_state.h"
#include "world.h"

namespace
{
// The pages, in PauseState.GetLabels' order (the secrets page only once a secret is held), then the cheats page
// that 20 Down presses past the last one open.
enum PageKind : uint8_t
{
    PAGE_MAP,
    PAGE_EQUIP,
    PAGE_CARDS,
    PAGE_SAVE,
    PAGE_CONFIG,
    PAGE_SECRET,
    PAGE_CHEATZ,
};

// MapSubstate.MapState
enum MapState : uint8_t
{
    MAP_RETURN,
    MAP_NO,
    MAP_YES,
};

// EquipSubstate.EquipState
enum EquipState : uint8_t
{
    EQUIP_BROOM,
    EQUIP_EXTEND,
    EQUIP_WIDEN,
    EQUIP_TRANSFORMER,
    EQUIP_SHOES,
    EQUIP_ITEM,
    EQUIP_KEY1,
    EQUIP_KEY2,
    EQUIP_KEY3,
};

constexpr int kCheatCounterMax = 20;
constexpr float kPlayerBlink = 0.4f;

// A page (Substate): made anew each time the selection moves onto it.
struct Page final
{
    uint8_t mKind;
    bool mExit;
    bool mSelectorVisible;
    uint8_t mState;
    uint8_t mLastState;
    float mTimer; // MapSubstate._playerIndicatorTimer
    uint8_t mBottom[5]; // EquipSubstate.bottom_row_enabled
    uint8_t mBottomCount;
    uint8_t mBottomIndex;
    bool mDialogue; // DialogueSubstate.InDialogueMode
};

struct PauseData final
{
    bool mOpen;
    bool mInSubstate;
    int mLastState;
    int mCheatCounter;
    Page mPage;
} gPause;

int gState = 0; // PauseState._state (static: the menu opens on the page it was left on)

void PlaySfx(const char* name)
{
    Game_PlaySfx(name);
}

// SoundManager.PlayPitchedSoundEffect: no random pick, so no RNG draw (the port plays it unpitched).
void PlayPitchedSfx(const char* name)
{
    const int32_t id = AudioManager_FindSfx(name);
    if (id >= 0)
    {
        AudioManager_PlaySFX(static_cast<uint32_t>(id));
    }
}

int PageCount()
{
    for (bool s : gGame.mSecretStatus)
    {
        if (s)
        {
            return 6;
        }
    }
    return 5;
}

// ---- Substate --------------------------------------------------------------------------------------------------
void ExitSubState(Page& p)
{
    p.mExit = true;
    p.mSelectorVisible = false;
    PlaySfx("menu_select");
}

void BaseGetControl(Page& p)
{
    p.mSelectorVisible = true;
}

void BaseHandleInput(Page& p)
{
    if (key_hit(KEY_B) || key_hit(KEY_LEFT))
    {
        ExitSubState(p);
    }
}

// ---- MapSubstate -----------------------------------------------------------------------------------------------
void MapGetControl(Page& p)
{
    if (gGame.mReturnMap < 0)
    {
        ExitSubState(p);
        return;
    }
    BaseGetControl(p);
    p.mState = MAP_RETURN;
}

void MapUpdate(Page& p)
{
    p.mLastState = p.mState;
    p.mTimer += kDeltaTime;
    if (p.mTimer > 2 * kPlayerBlink)
    {
        p.mTimer -= 2 * kPlayerBlink;
    }
}

void MapHandleInput(Page& p)
{
    switch (p.mState)
    {
    case MAP_RETURN:
        if (key_hit(KEY_A))
        {
            p.mState = MAP_NO;
            PlaySfx("menu_select");
        }
        else
        {
            BaseHandleInput(p);
        }
        break;
    case MAP_NO:
        if (key_hit(KEY_A) || key_hit(KEY_B))
        {
            p.mState = MAP_RETURN;
            PlaySfx("menu_select");
        }
        else if (key_hit(KEY_LEFT))
        {
            p.mState = MAP_YES;
            PlaySfx("menu_move");
        }
        else if (key_hit(KEY_RIGHT))
        {
            PlaySfx("menu_move");
        }
        break;
    case MAP_YES:
        if (key_hit(KEY_A))
        {
            PlaySfx("menu_select");
            // GlobalState.ReturnTarget.Warp(new Vector2(10, 34))
            World_RequestWarp(gGame.mReturnMap, gGame.mReturnX + 10, gGame.mReturnY + 34, 255);
            ExitSubState(p);
        }
        else if (key_hit(KEY_B))
        {
            p.mState = MAP_RETURN;
            PlaySfx("menu_select");
        }
        else if (key_hit(KEY_LEFT))
        {
            PlaySfx("menu_move");
        }
        else if (key_hit(KEY_RIGHT))
        {
            p.mState = MAP_NO;
            PlaySfx("menu_move");
        }
        break;
    default:
        break;
    }
}

// ---- EquipSubstate ---------------------------------------------------------------------------------------------
void EquipCreate(Page& p)
{
    p.mBottomCount = 0;
    if (gGame.mCanJump)
    {
        p.mBottom[p.mBottomCount++] = EQUIP_SHOES;
    }
    if (gGame.mTradeState != TRADE_NONE)
    {
        p.mBottom[p.mBottomCount++] = EQUIP_ITEM;
    }
    p.mBottom[p.mBottomCount++] = EQUIP_KEY1;
    p.mBottom[p.mBottomCount++] = EQUIP_KEY2;
    p.mBottom[p.mBottomCount++] = EQUIP_KEY3;
}

void EquipGetControl(Page& p)
{
    if (!Inventory_HasAnyBroom())
    {
        p.mExit = true;
        return;
    }
    BaseGetControl(p);
    p.mState = EQUIP_BROOM;
    p.mLastState = p.mState;
}

// DialogueSubstate.SetDialogue: the item's text in the menu's box.
void SetDialogue(Page& p, int id)
{
    const char* text = Dialog_GetFor("misc", "any", "items", id);
    if (text && text[0])
    {
        Dialog_Open(text, false, false);
        p.mDialogue = Dialog_IsOpen();
    }
}

void EquipBroom(Page& p, uint8_t type)
{
    Inventory_SetEquippedBroom(type);
    ExitSubState(p);
}

void EquipUseItem(Page& p)
{
    switch (p.mState)
    {
    case EQUIP_BROOM:
        EquipBroom(p, BROOM_NORMAL);
        break;
    case EQUIP_EXTEND:
        EquipBroom(p, BROOM_LONG);
        break;
    case EQUIP_WIDEN:
        EquipBroom(p, BROOM_WIDE);
        break;
    case EQUIP_TRANSFORMER:
        EquipBroom(p, BROOM_TRANSFORMER);
        break;
    case EQUIP_SHOES:
        SetDialogue(p, 5);
        break;
    case EQUIP_ITEM:
        SetDialogue(p, gGame.mTradeState == TRADE_SHOES ? 6 : 7);
        break;
    case EQUIP_KEY1:
    case EQUIP_KEY2:
    case EQUIP_KEY3:
        if (gGame.mBigKeyStatus[p.mState - EQUIP_KEY1])
        {
            SetDialogue(p, 8 + (p.mState - EQUIP_KEY1));
        }
        break;
    default:
        break;
    }
}

void EquipHandleInput(Page& p)
{
    if (p.mDialogue)
    {
        return;
    }
    if (key_hit(KEY_UP))
    {
        if (p.mState == EQUIP_BROOM)
        {
            return;
        }
        if (p.mState >= EQUIP_SHOES)
        {
            p.mState = EQUIP_TRANSFORMER;
            return;
        }
        p.mState--;
    }
    else if (key_hit(KEY_DOWN))
    {
        if (p.mState >= EQUIP_ITEM)
        {
            return;
        }
        if (p.mState == EQUIP_TRANSFORMER)
        {
            p.mState = p.mBottom[0];
            p.mBottomIndex = 0;
        }
        else if (p.mState < EQUIP_TRANSFORMER)
        {
            p.mState++;
        }
    }
    else if (p.mState >= EQUIP_SHOES && key_hit(KEY_RIGHT))
    {
        if (p.mBottomIndex == p.mBottomCount - 1)
        {
            return;
        }
        p.mState = p.mBottom[++p.mBottomIndex];
    }
    else if (p.mState > EQUIP_SHOES && p.mBottomIndex > 0 && key_hit(KEY_LEFT))
    {
        p.mState = p.mBottom[--p.mBottomIndex];
    }
    else if (key_hit(KEY_A))
    {
        EquipUseItem(p);
    }
    else
    {
        BaseHandleInput(p);
    }
}

void EquipUpdate(Page& p)
{
    // DialogueSubstate.Update: the box, then EquipSubstate's own
    if (p.mDialogue && !Dialog_Update())
    {
        p.mDialogue = false;
    }
    if (p.mLastState != p.mState)
    {
        p.mLastState = p.mState;
        PlaySfx("menu_move");
    }
}

// ---- The page ----------------------------------------------------------------------------------------------------
// (CardSubstate, SaveSubstate, ConfigSubstate, SecretSubstate and CheatzSubstate take control and give it back on
// Cancel / Left like any Substate; their own lines are still to be ported)
void PageGetControl(Page& p)
{
    switch (p.mKind)
    {
    case PAGE_MAP:
        MapGetControl(p);
        break;
    case PAGE_EQUIP:
        EquipGetControl(p);
        break;
    default:
        BaseGetControl(p);
        break;
    }
}

void PageHandleInput(Page& p)
{
    switch (p.mKind)
    {
    case PAGE_MAP:
        MapHandleInput(p);
        break;
    case PAGE_EQUIP:
        EquipHandleInput(p);
        break;
    default:
        BaseHandleInput(p);
        break;
    }
}

void PageUpdate(Page& p)
{
    switch (p.mKind)
    {
    case PAGE_MAP:
        MapUpdate(p);
        break;
    case PAGE_EQUIP:
        EquipUpdate(p);
        break;
    default:
        break;
    }
}

// PauseState.StateChanged: the page under the selection, made anew.
void StateChanged()
{
    PauseData& d = gPause;
    d.mLastState = gState;
    d.mPage = Page();
    d.mPage.mKind = gState == PageCount() ? PAGE_CHEATZ : static_cast<uint8_t>(gState);
    if (d.mPage.mKind == PAGE_EQUIP)
    {
        EquipCreate(d.mPage);
    }
}

// PauseState.BrowseInput: up and down the page list, Accept / Right into the page.
void BrowseInput()
{
    PauseData& d = gPause;
    if (key_hit(KEY_A) || key_hit(KEY_RIGHT))
    {
        PlaySfx("menu_select");
        d.mInSubstate = true;
        PageGetControl(d.mPage);
    }
    else if (key_hit(KEY_UP))
    {
        d.mCheatCounter = 0;
        if (gState == 0)
        {
            return;
        }
        PlaySfx("menu_move");
        gState--;
    }
    else if (key_hit(KEY_DOWN))
    {
        const int last = PageCount() - 1;
        if (gState == PageCount())
        {
            return;
        }
        if (gState == last)
        {
            PlaySfx("menu_move");
            d.mCheatCounter++;
            if (d.mCheatCounter == kCheatCounterMax)
            {
                d.mCheatCounter = 0;
                gState++;
            }
            return;
        }
        PlaySfx("menu_move");
        gState++;
    }
}
} // namespace

void Pause_Open()
{
    gPause = PauseData();
    gPause.mOpen = true;
    gPause.mLastState = gState;
    StateChanged();
}

bool Pause_IsOpen()
{
    return gPause.mOpen;
}

void Pause_Reset()
{
    gState = 0;
}

bool Pause_Update()
{
    PauseData& d = gPause;
    if (gGame.mCurHealth == 0)
    {
        d.mOpen = false;
        return false;
    }
    bool exit = false;
    if (key_hit(KEY_START) || (!d.mInSubstate && key_hit(KEY_B)))
    {
        PlayPitchedSfx("pause_sound");
        exit = true;
    }
    else if (!d.mInSubstate)
    {
        BrowseInput();
    }
    else
    {
        PageHandleInput(d.mPage);
        if (d.mPage.mExit)
        {
            d.mInSubstate = false;
            d.mPage.mExit = false;
            if (World_WarpPending())
            {
                // (the page started a warp: out of the menu)
                d.mOpen = false;
                return false;
            }
        }
    }
    PageUpdate(d.mPage);
    if (d.mLastState != gState)
    {
        StateChanged();
    }
    if (exit)
    {
        if (d.mPage.mDialogue)
        {
            Dialog_Close();
        }
        d.mOpen = false;
    }
    return d.mOpen;
}
