#include "pause.h"

#include <stdint.h>
#include <string.h>

#include "tonc.h"

#include "audio_manager.h"
#include "dialog.h"
#include "ecs.h"
#include "game_state.h"
#include "save.h"
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

#ifdef ANODYNE_SIM
constexpr int kCheatCounterMax = 2; // (the C# Debug build's: the recordings the simulator replays come from one)
#else
constexpr int kCheatCounterMax = 20;
#endif
constexpr float kPlayerBlink = 0.4f;

// ---- ConfigSubstate's menus -------------------------------------------------------------------------------------
enum MenuKind : uint8_t
{
    MENU_CONFIG,
    MENU_GAMEPLAY,
    MENU_GRAPHICS,
    MENU_CONTROLS,
};

// A ListSubstate: up and down its options, Accept gives the option control until it exits.
struct ListMenu final
{
    uint8_t mKind;
    bool mExit;
    uint8_t mState;
    uint8_t mLastState;
    int8_t mSelected; // _selectedOption (-1: none)
    // The selected option's own state (UIOption): an AudioSlider's value, a TextSelector's index.
    bool mOptionExit;
    float mCurrent;
    float mStart;
    uint8_t mIndex;
    uint8_t mStartIndex;
};

// ControlsSubstate: the key binding pages (looked through; a rebind waits for a key and keeps the GBA's buttons).
struct ControlsMenu final
{
    bool mExit;
    bool mOnConfirm;
    bool mRebinding;
    uint8_t mPage;
    uint8_t mPos;
};

struct ConfigData final
{
    ListMenu mList;
    ListMenu mSub; // the Gameplay / Graphics sub-menu (SubstateOption)
    ControlsMenu mControls;
};

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
    uint8_t mSaved;   // SaveSubstate: the quick save (1) / save (2) labels say "Saved!"
    int8_t mCardSel;  // CardSubstate.selectedID (negative: on the page setter, below that card)
    uint8_t mCardPage;
    ConfigData mConfig;
    uint8_t mGridSel;   // GridSubstate._selectedID (the secrets page)
    char mCheat[11];    // CheatzSubstate._cheatLabel
    uint8_t mCheatLen;
};

struct PauseData final
{
    bool mOpen;
    bool mInSubstate;
    int mLastState;
    int mCheatCounter;
    int mPageCount; // substates.Count: the pages as the menu opened
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

// ---- CardSubstate ----------------------------------------------------------------------------------------------
// Twelve cards a page (3 x 4), four pages: Left / Right run on across the pages, Down off the bottom row goes to the page
// setter ("1/4"...: Left / Right there turn the page, Up back to the cards), NextPage / PreviousPage turn it anywhere.
void CardsGetControl(Page& p)
{
    BaseGetControl(p);
    p.mCardSel = 0;
}

void CardsHandleInput(Page& p)
{
    if (p.mDialogue)
    {
        return;
    }
    if (key_hit(KEY_B))
    {
        ExitSubState(p);
        return;
    }
    if (p.mCardSel < 0)
    {
        if (key_hit(KEY_UP))
        {
            p.mCardSel = static_cast<int8_t>(-p.mCardSel);
            p.mSelectorVisible = true;
            PlaySfx("menu_select");
        }
        else if (key_hit(KEY_LEFT))
        {
            // TextSelector (noConfirm, noLoop)
            if (p.mCardPage > 0)
            {
                p.mCardPage--;
                PlaySfx("menu_move");
            }
        }
        else if (key_hit(KEY_RIGHT))
        {
            if (p.mCardPage < 3)
            {
                p.mCardPage++;
                PlaySfx("menu_move");
            }
        }
        return;
    }
    bool moved = false;
    if (key_hit(KEY_RIGHT))
    {
        if (p.mCardSel % 3 == 2)
        {
            if (p.mCardPage == 3)
            {
                return;
            }
            p.mCardSel = static_cast<int8_t>(p.mCardSel - 2);
            p.mCardPage++;
        }
        else
        {
            p.mCardSel++;
        }
        moved = true;
    }
    else if (key_hit(KEY_LEFT))
    {
        if (p.mCardSel % 3 == 0)
        {
            if (p.mCardPage == 0)
            {
                ExitSubState(p);
                return;
            }
            p.mCardSel = static_cast<int8_t>(p.mCardSel + 2);
            p.mCardPage--;
        }
        else
        {
            p.mCardSel--;
        }
        moved = true;
    }
    else if (key_hit(KEY_UP))
    {
        if (p.mCardSel < 3)
        {
            return;
        }
        p.mCardSel = static_cast<int8_t>(p.mCardSel - 3);
        moved = true;
    }
    else if (key_hit(KEY_DOWN))
    {
        if (p.mCardSel > 8)
        {
            p.mSelectorVisible = false;
            p.mCardSel = static_cast<int8_t>(-p.mCardSel);
            PlaySfx("menu_select");
            return;
        }
        p.mCardSel = static_cast<int8_t>(p.mCardSel + 3);
        moved = true;
    }
    else if (key_hit(KEY_A))
    {
        const int card = p.mCardPage * 12 + p.mCardSel;
        if (gGame.mCardStatus[card])
        {
            const char* text = Dialog_GetFor("card", "ETC", "one", card);
            if (text && text[0])
            {
                Dialog_Open(text, false, false);
                p.mDialogue = Dialog_IsOpen();
            }
        }
    }
    if (key_hit(KEY_R))
    {
        if (p.mCardPage == 3)
        {
            return;
        }
        p.mCardPage++;
        moved = true;
    }
    else if (key_hit(KEY_L))
    {
        if (p.mCardPage == 0)
        {
            return;
        }
        p.mCardPage--;
        moved = true;
    }
    if (moved)
    {
        PlaySfx("menu_move");
    }
}

// ---- SaveSubstate -----------------------------------------------------------------------------------------------
enum SaveState : uint8_t
{
    SAVE_QUICK_SAVE,
    SAVE_QUICK_LOAD,
    SAVE_SAVE,
    SAVE_TITLE,
    SAVE_QUIT,
    SAVE_JUST_QUIT,
};

void SaveGetControl(Page& p)
{
    BaseGetControl(p);
    p.mState = SAVE_QUICK_SAVE;
}

void SaveHandleInput(Page& p)
{
    if (key_hit(KEY_UP))
    {
        if (p.mState == SAVE_QUICK_SAVE)
        {
            return;
        }
        p.mState--;
    }
    else if (key_hit(KEY_DOWN))
    {
        if (p.mState >= SAVE_JUST_QUIT)
        {
            return;
        }
        p.mState++;
    }
    else if (key_hit(KEY_A))
    {
        bool playSound = true;
        switch (p.mState)
        {
        case SAVE_QUICK_SAVE:
            World_QuickSave();
            playSound = false;
            p.mSaved |= 1;
            break;
        case SAVE_QUICK_LOAD:
            playSound = false;
            World_RequestQuickLoad();
            break;
        case SAVE_SAVE:
            Save_Write(Save_Slot());
            p.mSaved |= 2;
            break;
        case SAVE_TITLE:
            Save_Write(Save_Slot());
            Game_RequestTitle();
            break;
        case SAVE_QUIT:
            // (GlobalState.ClosingGame: a cartridge has nothing to close to, so it goes to the title)
            Save_Write(Save_Slot());
            Game_RequestTitle();
            break;
        default:
            Game_RequestTitle();
            break;
        }
        if (playSound)
        {
            PlaySfx("menu_select");
        }
    }
    else
    {
        BaseHandleInput(p);
    }
}

void SaveUpdate(Page& p)
{
    if (p.mLastState != p.mState)
    {
        p.mLastState = p.mState;
        PlaySfx("menu_move");
    }
}

// ---- ConfigSubstate --------------------------------------------------------------------------------------------
enum OptionKind : uint8_t
{
    OPT_SUBMENU, // SubstateOption
    OPT_SLIDER,  // AudioSlider
    OPT_TEXT,    // TextSelector
    OPT_CHECK,   // CheckBox
    OPT_ACTION,  // ActionOption
};

struct OptionDef final
{
    uint8_t mKind;
    uint8_t mMenu = 0;
    float* mValue = nullptr;
    float mMin = 0.0f;
    float mMax = 1.0f;
    float mStep = 0.1f;
    uint8_t* mOption = nullptr;
    uint8_t mCount = 0;
    bool mNoConfirm = false;
    bool mNoLoop = false;
    bool* mFlag = nullptr;
};

// SpriteDrawer.MaxScale: the screen's size over 160x180 (as small as the recordings' virtual screen, which runs at 2x)
constexpr uint8_t kMaxScale = 2;
constexpr uint8_t kControlsPageSize[] = {8, 6, 8, 4, 4};
constexpr int kControlsPages = sizeof(kControlsPageSize);

int OptionCount(uint8_t menu)
{
    return menu == MENU_GRAPHICS ? 7 : 6;
}

OptionDef MakeSlider(float* value, float max, float step)
{
    OptionDef d{OPT_SLIDER};
    d.mValue = value;
    d.mMax = max;
    d.mStep = step;
    return d;
}

OptionDef MakeText(uint8_t* option, uint8_t count, bool noConfirm = false)
{
    OptionDef d{OPT_TEXT};
    d.mOption = option;
    d.mCount = count;
    d.mNoConfirm = noConfirm;
    d.mNoLoop = noConfirm;
    return d;
}

OptionDef MakeCheck(bool* flag)
{
    OptionDef d{OPT_CHECK};
    d.mFlag = flag;
    return d;
}

OptionDef MakeSubmenu(uint8_t menu)
{
    OptionDef d{OPT_SUBMENU};
    d.mMenu = menu;
    return d;
}

// The options in ConfigSubstate / GameplayMenu / GraphicsMenu.SetLabels' order.
OptionDef GetOption(uint8_t menu, int i)
{
    Settings& s = gSettings;
    if (menu == MENU_CONFIG)
    {
        switch (i)
        {
        case 0:
            return MakeSubmenu(MENU_CONTROLS);
        case 1:
            return MakeSlider(&s.mMusicVolume, 1.0f, 0.1f);
        case 2:
            return MakeSlider(&s.mSfxVolume, 1.0f, 0.1f);
        case 3:
            return MakeSubmenu(MENU_GAMEPLAY);
        case 4:
            return MakeSubmenu(MENU_GRAPHICS);
        default:
            return MakeText(&s.mLanguage, 5);
        }
    }
    if (menu == MENU_GAMEPLAY)
    {
        bool* const flags[] = {&s.mAutosave, &s.mPauseOnUnfocus, &s.mFastText, &s.mInvincible, &s.mGuaranteedHealth,
                               &s.mExtendedCoyote};
        return MakeCheck(flags[i]);
    }
    switch (i)
    {
    case 0:
        return MakeText(&s.mResolution, 3);
    case 1:
        return MakeText(&s.mScale, kMaxScale, true);
    case 2:
        return MakeText(&s.mFps, 3);
    case 3:
        return MakeSlider(&s.mFlashBrightness, 1.0f, 0.1f);
    case 4:
        return MakeSlider(&s.mFlashEasing, 0.2f, 0.02f);
    case 5:
        return OptionDef{OPT_ACTION};
    default:
        return MakeCheck(&s.mScreenshake);
    }
}

void ListGetControl(ListMenu& l, uint8_t kind)
{
    l = ListMenu();
    l.mKind = kind;
    l.mSelected = -1;
}

void ListExit(ListMenu& l)
{
    l.mExit = true;
    PlaySfx("menu_select");
}

// ---- ControlsSubstate --------------------------------------------------------------------------------------------
void ControlsSetCursorPos(ControlsMenu& c, int pos)
{
    if (pos < 0 || pos > kControlsPageSize[c.mPage])
    {
        return;
    }
    c.mPos = static_cast<uint8_t>(pos);
    c.mOnConfirm = pos == kControlsPageSize[c.mPage];
}

void ControlsGetControl(ControlsMenu& c)
{
    c = ControlsMenu();
    ControlsSetCursorPos(c, 0);
}

void ControlsPageChanged(ControlsMenu& c)
{
    const int size = kControlsPageSize[c.mPage];
    if (c.mOnConfirm)
    {
        ControlsSetCursorPos(c, size);
    }
    else if (c.mPos > size - 1)
    {
        ControlsSetCursorPos(c, size - 1);
    }
}

void ControlsHandleInput(ControlsMenu& c)
{
    if (c.mRebinding)
    {
        // ExitRebind: whichever key comes next (a GBA keeps its buttons where they are)
        if (key_hit(KEY_ANY))
        {
            c.mRebinding = false;
            PlaySfx("menu_select");
        }
        return;
    }
    if (key_hit(KEY_B))
    {
        c.mExit = true;
        PlaySfx("menu_select");
        return;
    }
    bool moved = false;
    if (key_hit(KEY_UP))
    {
        ControlsSetCursorPos(c, c.mPos - 1);
        moved = true;
    }
    else if (key_hit(KEY_DOWN))
    {
        ControlsSetCursorPos(c, c.mPos + 1);
        moved = true;
    }
    else if (key_hit(KEY_RIGHT))
    {
        if (c.mPage == kControlsPages - 1)
        {
            return;
        }
        moved = true;
        c.mPage++;
        ControlsPageChanged(c);
    }
    else if (key_hit(KEY_LEFT))
    {
        if (c.mPage == 0)
        {
            return;
        }
        moved = true;
        c.mPage--;
        ControlsPageChanged(c);
    }
    else if (key_hit(KEY_A))
    {
        if (c.mOnConfirm)
        {
            c.mExit = true;
        }
        else
        {
            c.mRebinding = true;
        }
        PlaySfx("menu_select");
    }
    if (moved)
    {
        PlaySfx("menu_move");
    }
}

void ListHandleInput(ConfigData& c, ListMenu& l, bool leftExits);
void ListUpdate(ListMenu& l);

// ---- The options (UIOption) ----------------------------------------------------------------------------------------
void OptionGetControl(ConfigData& c, ListMenu& l, const OptionDef& d)
{
    switch (d.mKind)
    {
    case OPT_SUBMENU:
        if (d.mMenu == MENU_CONTROLS)
        {
            ControlsGetControl(c.mControls);
        }
        else
        {
            ListGetControl(c.mSub, d.mMenu);
        }
        break;
    case OPT_SLIDER:
        l.mCurrent = *d.mValue;
        l.mStart = l.mCurrent;
        break;
    case OPT_TEXT:
        l.mIndex = *d.mOption;
        l.mStartIndex = l.mIndex;
        break;
    case OPT_CHECK:
        *d.mFlag = !*d.mFlag;
        l.mOptionExit = true;
        break;
    default:
        // GraphicsMenu's flash test
        Screen_Flash(FxF(1.0f), FLASH_WHITE);
        l.mOptionExit = true;
        break;
    }
}

// OptionSelector.Update: Left / Right change it, Accept keeps it (SetValue), Cancel puts it back (ResetValue).
void SliderUpdate(ListMenu& l, const OptionDef& d)
{
    if (key_hit(KEY_LEFT))
    {
        l.mCurrent -= d.mStep;
        if (l.mCurrent <= d.mMin)
        {
            l.mCurrent = d.mMin;
        }
        PlaySfx("menu_move");
        *d.mValue = l.mCurrent;
    }
    else if (key_hit(KEY_RIGHT))
    {
        l.mCurrent += d.mStep;
        if (l.mCurrent >= d.mMax)
        {
            l.mCurrent = d.mMax;
        }
        PlaySfx("menu_move");
        *d.mValue = l.mCurrent;
    }
    else if (key_hit(KEY_A))
    {
        l.mStart = l.mCurrent;
        PlaySfx("menu_select");
        l.mOptionExit = true;
    }
    else if (key_hit(KEY_B))
    {
        l.mCurrent = l.mStart;
        *d.mValue = l.mCurrent;
        PlaySfx("menu_select");
        l.mOptionExit = true;
    }
}

void TextSetValue(ListMenu& l, const OptionDef& d)
{
    l.mStartIndex = l.mIndex;
    *d.mOption = l.mIndex;
    if (!d.mNoConfirm)
    {
        PlaySfx("menu_select");
    }
}

void TextUpdate(ListMenu& l, const OptionDef& d)
{
    if (key_hit(KEY_LEFT) || key_hit(KEY_RIGHT))
    {
        const bool left = key_hit(KEY_LEFT);
        const uint8_t end = left ? 0 : d.mCount - 1;
        if (l.mIndex == end)
        {
            if (d.mNoLoop)
            {
                return;
            }
            l.mIndex = left ? d.mCount - 1 : 0;
        }
        else
        {
            l.mIndex = left ? l.mIndex - 1 : l.mIndex + 1;
        }
        if (d.mNoConfirm)
        {
            TextSetValue(l, d);
        }
        PlaySfx("menu_move");
    }
    else if (key_hit(KEY_A))
    {
        TextSetValue(l, d);
        l.mOptionExit = true;
    }
    else if (key_hit(KEY_B))
    {
        l.mIndex = l.mStartIndex;
        *d.mOption = l.mIndex;
        l.mOptionExit = true;
    }
}

void OptionUpdate(ConfigData& c, ListMenu& l, const OptionDef& d)
{
    switch (d.mKind)
    {
    case OPT_SUBMENU:
        if (d.mMenu == MENU_CONTROLS)
        {
            ControlsHandleInput(c.mControls);
            if (c.mControls.mExit)
            {
                l.mOptionExit = true;
            }
        }
        else
        {
            ListHandleInput(c, c.mSub, false);
            ListUpdate(c.mSub);
            if (c.mSub.mExit)
            {
                l.mOptionExit = true;
            }
        }
        break;
    case OPT_SLIDER:
        SliderUpdate(l, d);
        break;
    case OPT_TEXT:
        TextUpdate(l, d);
        break;
    default:
        break;
    }
}

// ---- ListSubstate --------------------------------------------------------------------------------------------------
void ListHandleInput(ConfigData& c, ListMenu& l, bool leftExits)
{
    if (l.mSelected >= 0)
    {
        OptionUpdate(c, l, GetOption(l.mKind, l.mSelected));
        if (l.mOptionExit)
        {
            l.mOptionExit = false;
            l.mSelected = -1;
            l.mState = l.mLastState;
        }
        return;
    }
    if (key_hit(KEY_UP))
    {
        if (l.mState == 0)
        {
            return;
        }
        l.mState--;
    }
    else if (key_hit(KEY_DOWN))
    {
        if (l.mState >= OptionCount(l.mKind) - 1)
        {
            return;
        }
        l.mState++;
    }
    else if (key_hit(KEY_A))
    {
        l.mSelected = static_cast<int8_t>(l.mState);
        OptionGetControl(c, l, GetOption(l.mKind, l.mState));
        PlaySfx("menu_select");
    }
    else if (key_hit(KEY_B) || (leftExits && key_hit(KEY_LEFT)))
    {
        ListExit(l);
    }
}

void ListUpdate(ListMenu& l)
{
    if (l.mLastState != l.mState)
    {
        l.mLastState = l.mState;
        PlaySfx("menu_move");
    }
}

void ConfigGetControl(Page& p)
{
    BaseGetControl(p);
    ListMenu& l = p.mConfig.mList;
    l.mKind = MENU_CONFIG;
    l.mSelected = -1;
    l.mLastState = l.mState;
}

void ConfigHandleInput(Page& p)
{
    ListMenu& l = p.mConfig.mList;
    ListHandleInput(p.mConfig, l, true);
    if (l.mExit)
    {
        l.mExit = false;
        p.mExit = true;
        p.mSelectorVisible = false;
    }
}

// ---- SecretSubstate (GridSubstate) ---------------------------------------------------------------------------------
// The secrets, four a row: Left off the first column, or Cancel, exits.
constexpr int kSecrets = sizeof(gGame.mSecretStatus);

void SecretsGetControl(Page& p)
{
    BaseGetControl(p);
    p.mGridSel = 0;
}

void SecretsHandleInput(Page& p)
{
    if (p.mDialogue)
    {
        return;
    }
    bool moved = false;
    if (key_hit(KEY_RIGHT))
    {
        if (p.mGridSel % 4 == 3 || p.mGridSel == kSecrets - 1)
        {
            return;
        }
        p.mGridSel++;
        moved = true;
    }
    else if (key_hit(KEY_LEFT))
    {
        if (p.mGridSel % 4 == 0)
        {
            ExitSubState(p);
            return;
        }
        p.mGridSel--;
        moved = true;
    }
    else if (key_hit(KEY_UP))
    {
        if (p.mGridSel < 4)
        {
            return;
        }
        p.mGridSel -= 4;
        moved = true;
    }
    else if (key_hit(KEY_DOWN))
    {
        p.mGridSel = p.mGridSel > kSecrets - 5 ? kSecrets - 1 : p.mGridSel + 4;
        moved = true;
    }
    else if (key_hit(KEY_B))
    {
        ExitSubState(p);
    }
    else if (key_hit(KEY_A))
    {
        if (gGame.mSecretStatus[p.mGridSel])
        {
            const char* text = Dialog_GetFor("misc", "any", "secrets", p.mGridSel);
            if (text && text[0])
            {
                Dialog_Open(text, false, false);
                p.mDialogue = Dialog_IsOpen();
            }
        }
    }
    if (moved)
    {
        PlaySfx("menu_move");
    }
}

// ---- CheatzSubstate ----------------------------------------------------------------------------------------------
// Ten presses make a code (U R D L, 1 Accept, 2 Cancel); one that isn't a cheat costs two hearts.
void CheatKonami()
{
    PlaySfx("unlock");
    GameState_SetMaxHealth(16);
    GameState_SetHealth(gGame.mMaxHealth);
    for (int b = BROOM_NORMAL; b <= BROOM_TRANSFORMER; b++)
    {
        gGame.mHasBroom[b] = true;
    }
    gGame.mCanJump = true;
    memset(gGame.mCardStatus, 1, sizeof(gGame.mCardStatus));
    for (bool& k : gGame.mBigKeyStatus)
    {
        k = true;
    }
    for (bool& k : gGame.mSecretStatus)
    {
        k = true;
    }
    Inventory_SetEquippedBroom(gGame.mEquippedBroom == BROOM_NONE ? static_cast<uint8_t>(BROOM_NORMAL) : gGame.mEquippedBroom);
}

void ToggleChaos(uint8_t mode)
{
    gGame.mGameMode = gGame.mGameMode == mode ? static_cast<uint8_t>(GAME_MODE_NORMAL) : mode;
    World_ForceTextureReload();
    if (gGame.mGameMode == mode)
    {
        PlaySfx("shieldy-hit");
        PlaySfx("small_wave");
        PlaySfx("big_door_locked");
        if (mode == GAME_MODE_EXTREME_CHAOS)
        {
            PlaySfx("fall_in_hole");
        }
    }
}

// CheatzManager.DoCheat
void DoCheat(const char* code)
{
    if (strcmp(code, "LRLR121212") == 0)
    {
        gFuckItMode = !gFuckItMode;
    }
    else if (strcmp(code, "UUDDLRLR21") == 0)
    {
        CheatKonami();
    }
    else if (strcmp(code, "UDUD121212") == 0)
    {
        gGame.mNexusPortals = ~0u;
        PlaySfx("menu_select");
        PlaySfx("keyget");
    }
    else if (strcmp(code, "URLDURLD11") == 0)
    {
        gGame.mAlwaysCellGraphics = !gGame.mAlwaysCellGraphics;
        World_ForceTextureReload();
    }
    else if (strcmp(code, "12UULLRRDD") == 0)
    {
        Inventory_AddMapKey();
        PlaySfx("keyget");
    }
    else if (strcmp(code, "URDLURDLUR") == 0)
    {
        ToggleChaos(GAME_MODE_CHAOS);
    }
    else if (strcmp(code, "ULDRULDRUL") == 0)
    {
        ToggleChaos(GAME_MODE_EXTREME_CHAOS);
    }
    else
    {
        GameState_SetHealth(gGame.mCurHealth - 2);
        PlaySfx("sb_hurt");
    }
}

void CheatzHandleInput(Page& p)
{
    char c = 0;
    if (key_hit(KEY_UP))
    {
        c = 'U';
    }
    else if (key_hit(KEY_RIGHT))
    {
        c = 'R';
    }
    else if (key_hit(KEY_DOWN))
    {
        c = 'D';
    }
    else if (key_hit(KEY_LEFT))
    {
        c = 'L';
    }
    else if (key_hit(KEY_A))
    {
        c = '1';
    }
    else if (key_hit(KEY_B))
    {
        c = '2';
    }
    if (c)
    {
        PlaySfx("menu_select");
        p.mCheat[p.mCheatLen++] = c;
        p.mCheat[p.mCheatLen] = '\0';
    }
    if (p.mCheatLen == 10)
    {
        DoCheat(p.mCheat);
        p.mCheatLen = 0;
        p.mCheat[0] = '\0';
        ExitSubState(p);
    }
}

// DialogueSubstate.Update
void DialogueUpdate(Page& p)
{
    if (p.mDialogue && !Dialog_Update())
    {
        p.mDialogue = false;
    }
}

// ---- The page ----------------------------------------------------------------------------------------------------
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
    case PAGE_CARDS:
        CardsGetControl(p);
        break;
    case PAGE_SAVE:
        SaveGetControl(p);
        break;
    case PAGE_CONFIG:
        ConfigGetControl(p);
        break;
    case PAGE_SECRET:
        SecretsGetControl(p);
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
    case PAGE_CARDS:
        CardsHandleInput(p);
        break;
    case PAGE_SAVE:
        SaveHandleInput(p);
        break;
    case PAGE_CONFIG:
        ConfigHandleInput(p);
        break;
    case PAGE_SECRET:
        SecretsHandleInput(p);
        break;
    case PAGE_CHEATZ:
        CheatzHandleInput(p);
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
    case PAGE_CARDS:
    case PAGE_SECRET:
        DialogueUpdate(p);
        break;
    case PAGE_SAVE:
        SaveUpdate(p);
        break;
    case PAGE_CONFIG:
        ListUpdate(p.mConfig.mList);
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
    d.mPage.mKind = gState == d.mPageCount ? PAGE_CHEATZ : static_cast<uint8_t>(gState);
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
        const int last = d.mPageCount - 1;
        if (gState == d.mPageCount)
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
    gPause.mPageCount = PageCount();
    gPause.mLastState = gState;
    StateChanged();
}

bool Pause_IsOpen()
{
    return gPause.mOpen;
}

void Pause_Close()
{
    gPause.mOpen = false;
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
