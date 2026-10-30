#include "pause.h"

#include <stdint.h>
#include <string.h>

#include "tonc.h"

#include "audio_manager.h"
#include "dialog.h"
#include "ecs.h"
#include "game_state.h"
#include "pause_draw.h"
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

#if defined(ANODYNE_SIM) || defined(ANODYNE_LINK)
constexpr int kCheatCounterMax = 2; // (the C# Debug build's: the recordings the simulator and link ROM replay come from one)
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
    int mFlashFrames; // the page selector's "flash" anim, since it last started
    int mSelFrames;   // the page's selectors' anims, since it took control
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
    p.mTimer += kUiDeltaTime;
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
    return menu == MENU_GRAPHICS ? 8 : 6;
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
    case 6:
        return MakeCheck(&s.mScreenshake);
    default:
        return MakeCheck(&s.mWideView);
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
        d.mSelFrames = 0;
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

// ---- Drawing (PauseState.DrawUI and the pages') -----------------------------------------------------------------
constexpr int kSelectorW = 7; // MenuSelector's sprite width

// DialogueManager.GetDialogue("misc", "any", scene, id), looked up once (a lookup walks the scene table).
struct MiscText final
{
    const char* mScene;
    int mId;
    const char* mText;
};
MiscText gMiscText[64] __attribute__((section(".sbss"))); // (EWRAM)
int gMiscTexts = 0;

const char* Misc(const char* scene, int id)
{
    for (int i = 0; i < gMiscTexts; i++)
    {
        if (gMiscText[i].mId == id && strcmp(gMiscText[i].mScene, scene) == 0)
        {
            return gMiscText[i].mText;
        }
    }
    const char* text = Dialog_Peek("misc", "any", scene, id);
    if (gMiscTexts < static_cast<int>(sizeof(gMiscText) / sizeof(gMiscText[0])))
    {
        gMiscText[gMiscTexts++] = {scene, id, text};
    }
    return text;
}

// A number's digits after a text (the labels the C# builds with string concatenation).
char* AppendInt(char* out, int v)
{
    char digits[12];
    int n = 0;
    unsigned u = v < 0 ? static_cast<unsigned>(-v) : static_cast<unsigned>(v);
    do
    {
        digits[n++] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
    {
        *out++ = '-';
    }
    while (n)
    {
        *out++ = digits[--n];
    }
    *out = '\0';
    return out;
}

char* AppendText(char* out, const char* text)
{
    while (*text)
    {
        *out++ = *text++;
    }
    *out = '\0';
    return out;
}

// MenuSelector's anims at 6fps: enabledRight 3, 2; enabledLeft 0, 1.
int ArrowFrame(bool left)
{
    const int phase = (gPause.mSelFrames / 10) & 1;
    return left ? phase : 3 - phase;
}

// A Substate's selector, right of the label it points at (label position - (width, -2)).
void DrawLabelSelector(int x, int y)
{
    MenuDraw_Image(MENU_IMG_ARROWS, ArrowFrame(false), x - kSelectorW, y + 2);
}

// OptionSelector.Draw: its arrows while it has control.
void DrawOptionArrows(int x, int y, int width)
{
    MenuDraw_Image(MENU_IMG_ARROWS, ArrowFrame(true), x, y + 2);
    MenuDraw_Image(MENU_IMG_ARROWS, ArrowFrame(false), x + width, y + 2);
}

// AudioSlider.Draw: the background, the fill, then the frame (the C# draws the 1x1 textures' rectangles from their
// top-left corners, since their origin rounds to 0).
void DrawSlider(int x, int y, float current, float min, float max, int style, bool control)
{
    MenuDraw_Fill(x + 8, y, 60, 12, MENU_VOLUME_BG);
    if (current > min)
    {
        const int w = static_cast<int>(60 * (current - min) / (max - min));
        MenuDraw_Fill(x + 8, y, w, 12, MENU_VOLUME_INSIDE);
    }
    MenuDraw_Image(MENU_IMG_VOLUME_BAR, style, x + 8, y);
    if (control)
    {
        DrawOptionArrows(x, y, 68);
    }
}

// TextSelector.Draw: the option's text 8px in, the arrows while it has control.
void DrawTextSelector(int x, int y, int width, const char* text, bool control)
{
    MenuDraw_Text(text, x + 8, y);
    if (control)
    {
        DrawOptionArrows(x, y, width);
    }
}

void DrawMapPage(const Page& p)
{
    const bool dungeon = gGame.mReturnMap >= 0 && gGame.mReturnMap != Game_MapIndex("NEXUS");
    const int x = 73;
    const int y = 144;
    if (gGame.mReturnMap >= 0)
    {
        MenuDraw_Text(Misc("map", dungeon ? 5 : 4), x, y - 16);
    }
    if (p.mState != MAP_RETURN)
    {
        MenuDraw_Text(Misc("checkpoint", 1), x, y + 4);
        MenuDraw_Text(Misc("checkpoint", 2), x + 56, y + 4);
    }
    if (!MenuDraw_Minimap(110, 70, p.mTimer < kPlayerBlink))
    {
        MenuDraw_Text(Misc("map", 3), x + 14, 31);
    }
    if (p.mSelectorVisible)
    {
        DrawLabelSelector(p.mState == MAP_RETURN ? x : (p.mState == MAP_NO ? x + 56 : x), p.mState == MAP_RETURN ? y - 16 : y + 4);
    }
}

void DrawEquipPage(const Page& p)
{
    const MenuImage icons[] = {MENU_IMG_NONE_ICON, MENU_IMG_LONG_ICON, MENU_IMG_WIDE_ICON, MENU_IMG_TRANSFORMER_ICON};
    const uint8_t brooms[] = {BROOM_NORMAL, BROOM_LONG, BROOM_WIDE, BROOM_TRANSFORMER};
    const int names[] = {1, 3, 4, 2};
    for (int i = 0; i < 4; i++)
    {
        const int y = 25 + 24 * i;
        const bool has = Inventory_HasBroomType(brooms[i]);
        if (has)
        {
            MenuDraw_Image(icons[i], 0, 65, y);
        }
        MenuDraw_Text(has ? Misc("items", names[i]) : "-", 87, y + 3);
        if (Inventory_EquippedBroom() == brooms[i])
        {
            MenuDraw_Image(MENU_IMG_EQUIPPED_ICON, 0, 65 + 12, y - 1);
        }
    }
    if (gGame.mCanJump)
    {
        MenuDraw_Image(MENU_IMG_JUMP_SHOES, 0, 62, 130);
    }
    if (gGame.mTradeState != TRADE_NONE)
    {
        MenuDraw_Image(MENU_IMG_TRADE_ITEMS, gGame.mTradeState == TRADE_BOX ? 1 : 0, 78, 130);
    }
    for (int i = 0; i < 3; i++)
    {
        MenuDraw_Image(MENU_IMG_BIG_KEYS, gGame.mBigKeyStatus[i] ? i * 2 : i * 2 + 1, 95 + 16 * i, 130);
    }
    if (!p.mSelectorVisible)
    {
        return;
    }
    if (p.mState <= EQUIP_TRANSFORMER)
    {
        DrawLabelSelector(87, 25 + 24 * p.mState + 3);
    }
    else
    {
        const int x = p.mState == EQUIP_SHOES ? 62 : (p.mState == EQUIP_ITEM ? 78 : 95 + 16 * (p.mState - EQUIP_KEY1));
        MenuDraw_Image(MENU_IMG_ARROWS, ArrowFrame(false), x, 130);
    }
}

void DrawCardsPage(const Page& p)
{
    static char sCount[24];
    for (int i = 0; i < 12; i++)
    {
        const int card = p.mCardPage * 12 + i;
        MenuDraw_Image(MENU_IMG_CARDS, card < static_cast<int>(sizeof(gGame.mCardStatus)) && gGame.mCardStatus[card] ? card : 49,
                       68 + (i % 3) * 30, 28 + (i / 3) * 30);
    }
    char* end = AppendInt(sCount, Inventory_CardCount());
    *end++ = ' ';
    AppendText(end, Misc("cards", 1));
    MenuDraw_Text(sCount, 70, 146);
    static const char* const kPages[] = {"1/4", "2/4", "3/4", "4/4"};
    DrawTextSelector(91, 156, 32, kPages[p.mCardPage], p.mCardSel < 0);
    if (p.mSelectorVisible && p.mCardSel >= 0)
    {
        MenuDraw_Image(MENU_IMG_ARROWS, ArrowFrame(false), 68 + (p.mCardSel % 3) * 30 - 10, 28 + (p.mCardSel / 3) * 30 + 8);
    }
}

void DrawSavePage(const Page& p)
{
    static char sDeaths[24];
    static const int kY[] = {28, 40, 52, 64, 86, 100};
    const char* const labels[] = {
        (p.mSaved & 1) ? Misc("save", 1) : Misc("controls", 15),
        Misc("controls", 16),
        (p.mSaved & 2) ? Misc("save", 1) : Misc("save", 0),
        Misc("save", 3),
        Misc("save", 5),
        Misc("save", 6),
    };
    for (int i = 0; i < 6; i++)
    {
        MenuDraw_Text(labels[i], 69, kY[i]);
    }
    AppendInt(AppendText(sDeaths, Misc("save", 7)), World_Deaths());
    MenuDraw_Text(sDeaths, 69, 130);
    if (p.mSelectorVisible)
    {
        DrawLabelSelector(69, kY[p.mState]);
    }
}

// ---- The config menus' pictures --------------------------------------------------------------------------------
struct OptionPos final
{
    int16_t mLabelX, mLabelY;
    int16_t mX, mY; // the option's own position
    int16_t mWidth; // a TextSelector's
};

const char* OptionLabel(uint8_t menu, int i)
{
    static const int kConfig[] = {1, 23, 24, 25, 26, 17};
    switch (menu)
    {
    case MENU_CONFIG:
        return Misc("config", kConfig[i]);
    case MENU_GAMEPLAY:
        return i == 0 ? Misc("config", 3) : Misc("gameplay", i - 1);
    default:
        if (i == 7)
        {
            return "Wide view"; // (not the C#'s: wide.diff's, in English only)
        }
        return i == 0 ? Misc("config", 12) : (i == 1 ? Misc("config", 16) : Misc("graphics", i == 2 ? 0 : i + 2));
    }
}

OptionPos GetOptionPos(uint8_t menu, int i)
{
    if (menu == MENU_CONFIG)
    {
        static const int16_t kY[] = {28, 44, 56, 72, 88, 104};
        const int16_t w = static_cast<int16_t>(MenuDraw_TextWidth(OptionLabel(menu, i)));
        // the sliders 5px short of the label's end, the language 8px
        return {69, kY[i], static_cast<int16_t>(69 + w - (i == 5 ? 8 : 5)), kY[i], 30};
    }
    if (menu == MENU_GAMEPLAY)
    {
        // (the C#'s float rows: text truncated as it's drawn, the check boxes rounded)
        static const int16_t kY[] = {17, 40, 60, 73, 86, 99};
        static const int16_t kBoxY[] = {20, 41, 60, 73, 86, 99};
        return {22, kY[i], 132, kBoxY[i], 0};
    }
    static const OptionPos kGraphics[] = {
        {22, 7, 32, 18, 90}, {22, 29, 72, 29, 20}, {22, 40, 52, 40, 80}, {30, 67, 70, 67, 0},
        {30, 78, 70, 78, 0}, {30, 89, 0, 0, 0},    {22, 105, 132, 105, 0}, {22, 116, 132, 116, 0},
    };
    return kGraphics[i];
}

const char* OptionText(uint8_t menu, int i, uint8_t index)
{
    if (menu == MENU_CONFIG)
    {
        static const char* const kLanguages[] = {"en", "es", "it", "pt-br", "zh-cn"};
        return kLanguages[index < 5 ? index : 0];
    }
    if (i == 0)
    {
        return Misc("config", 13 + index);
    }
    if (i == 1)
    {
        static const char* const kScales[] = {"1x", "2x", "3x", "4x"};
        return kScales[index < 4 ? index : 0];
    }
    return Misc("graphics", 1 + index);
}

void DrawList(const ListMenu& l, uint8_t style, bool selector)
{
    for (int i = 0; i < OptionCount(l.mKind); i++)
    {
        const OptionPos pos = GetOptionPos(l.mKind, i);
        MenuDraw_Text(OptionLabel(l.mKind, i), pos.mLabelX, pos.mLabelY);
        const OptionDef d = GetOption(l.mKind, i);
        const bool control = l.mSelected == i;
        switch (d.mKind)
        {
        case OPT_SLIDER:
            DrawSlider(pos.mX, pos.mY, control ? l.mCurrent : *d.mValue, d.mMin, d.mMax, style, control);
            break;
        case OPT_TEXT:
            DrawTextSelector(pos.mX, pos.mY, pos.mWidth, OptionText(l.mKind, i, control ? l.mIndex : *d.mOption), control);
            break;
        case OPT_CHECK:
            MenuDraw_Image(MENU_IMG_CHECKBOX, (*d.mFlag ? 1 : 0) + style * 2, pos.mX, pos.mY);
            break;
        default:
            break;
        }
    }
    if (selector && l.mSelected < 0)
    {
        const OptionPos pos = GetOptionPos(l.mKind, l.mState);
        DrawLabelSelector(pos.mLabelX, pos.mLabelY);
    }
}

// ControlsSubstate: the bindings, two rows each (a GBA's buttons in the keyboard column; they can't be moved).
void DrawControls(const ControlsMenu& c)
{
    struct Binding final
    {
        uint8_t mName; // its name's controls line - 1
        const char* mButton;
    };
    static const Binding kPages[kControlsPages][4] = {
        {{1, "Up"}, {4, "Right"}, {2, "Down"}, {3, "Left"}}, // (CreateTup's num)
        {{5, "B"}, {6, "A"}, {7, "Start"}, {0, nullptr}},
        {{8, "-"}, {9, "-"}, {10, "-"}, {11, "-"}},
        {{12, "R"}, {13, "L"}, {0, nullptr}, {0, nullptr}},
        {{14, "-"}, {15, "-"}, {0, nullptr}, {0, nullptr}},
    };
    MenuDraw_Image(MENU_IMG_CONTROLS_BOX, 0, 10, 8);
    MenuDraw_Text(Misc("controls", 17 + c.mPage), 20, 16);
    for (int i = 0; i < 4; i++)
    {
        const Binding& b = kPages[c.mPage][i];
        if (!b.mButton)
        {
            continue;
        }
        const int y = 16 + 16 * (1 + i * 2);
        MenuDraw_Text(Misc("controls", 1 + b.mName), 20, y);
        MenuDraw_Text(b.mButton, 90, y);
    }
    if (c.mRebinding)
    {
        MenuDraw_Fill(90, 16 + 16 * (1 + c.mPos), 40, 10, MENU_BLACK);
        MenuDraw_Text("_", 90, 16 + 16 * (1 + c.mPos));
    }
    MenuDraw_Text(Misc("controls", 22), 20, 156);
    static const char* const kPageNames[] = {"1/5", "2/5", "3/5", "4/5", "5/5"};
    DrawTextSelector(91, 156, 32, kPageNames[c.mPage], false);
    // (the selector points at a binding's row: the function's label, or the confirm label)
    DrawLabelSelector(20, c.mOnConfirm ? 156 : 16 + 16 * (1 + c.mPos));
}

void DrawConfigPage(const Page& p)
{
    ListMenu l = p.mConfig.mList;
    if (!p.mSelectorVisible)
    {
        l.mSelected = -1; // (until the page has control, nothing is)
    }
    DrawList(l, 0, p.mSelectorVisible);
    if (l.mSelected < 0)
    {
        return;
    }
    const OptionDef d = GetOption(MENU_CONFIG, l.mSelected);
    if (d.mKind != OPT_SUBMENU)
    {
        return;
    }
    if (d.mMenu == MENU_CONTROLS)
    {
        DrawControls(p.mConfig.mControls);
        return;
    }
    // GameplayMenu / GraphicsMenu: the box, the list (and GraphicsMenu's "Screen flash" heading)
    MenuDraw_Image(MENU_IMG_POP_MENU, 0, 12, d.mMenu == MENU_GAMEPLAY ? 10 : 5);
    if (d.mMenu == MENU_GRAPHICS)
    {
        MenuDraw_Text(Misc("graphics", 4), 22, 56);
    }
    DrawList(p.mConfig.mSub, 1, true);
}

void DrawSecretsPage(const Page& p)
{
    for (int i = 0; i < kSecrets; i++)
    {
        MenuDraw_Image(MENU_IMG_SECRETS, gGame.mSecretStatus[i] ? i : kSecrets, 68 + (i % 4) * 22, 28 + (i / 4) * 22);
    }
    if (p.mSelectorVisible)
    {
        MenuDraw_Image(MENU_IMG_ARROWS, ArrowFrame(false), 68 + (p.mGridSel % 4) * 22 - 8, 28 + (p.mGridSel / 4) * 22 + 5);
    }
}

void DrawCheatzPage(const Page& p)
{
    MenuDraw_Text(p.mCheat, 69, 44);
    if (p.mSelectorVisible)
    {
        DrawLabelSelector(69, 44);
    }
}

MenuGroup PageGroup(uint8_t kind)
{
    switch (kind)
    {
    case PAGE_MAP:
        return MENU_GROUP_MAP;
    case PAGE_EQUIP:
        return MENU_GROUP_EQUIP;
    case PAGE_CARDS:
        return MENU_GROUP_CARDS;
    case PAGE_CONFIG:
        return MENU_GROUP_CONFIG;
    case PAGE_SECRET:
        return MENU_GROUP_SECRETS;
    default:
        return MENU_GROUP_NONE;
    }
}

// PauseState.DrawUI: the background, the page list and its selector, the play time, the input help, then the page.
void Draw()
{
    static char sTime[12];
    PauseData& d = gPause;
    const Page& p = d.mPage;
    MenuDraw_Begin(PageGroup(p.mKind));
    MenuDraw_Image(MENU_IMG_BG, 0, 0, 20);
    MenuDraw_Image(MENU_IMG_PAGE_SELECTOR, d.mInSubstate ? 2 : (d.mFlashFrames / 15) & 1, 0, 30 + gState * 16);
    static const char* const kPageScenes[] = {"map", "items", "cards", "save", "config"};
    for (int i = 0; i < d.mPageCount; i++)
    {
        MenuDraw_Text(i < 5 ? Misc(kPageScenes[i], 0) : "???", 10, 31 + 16 * i);
    }
    const uint32_t seconds = gGame.mPlayFrames / 60;
    const uint32_t hms[] = {seconds / 3600 % 100, seconds / 60 % 60, seconds % 60};
    for (int i = 0; i < 3; i++)
    {
        sTime[i * 3] = static_cast<char>('0' + hms[i] / 10);
        sTime[i * 3 + 1] = static_cast<char>('0' + hms[i] % 10);
        sTime[i * 3 + 2] = i < 2 ? ':' : '\0';
    }
    MenuDraw_Text(sTime, 1, 154);
    static char sInput[48];
    if (!sInput[0])
    {
        char* end = AppendText(sInput, Misc("secrets", 14));
        *end++ = ' ';
        AppendText(end, Misc("secrets", 15));
    }
    MenuDraw_Text(sInput, 2, 168, MENU_GREY, false);
    switch (p.mKind)
    {
    case PAGE_MAP:
        DrawMapPage(p);
        break;
    case PAGE_EQUIP:
        DrawEquipPage(p);
        break;
    case PAGE_CARDS:
        DrawCardsPage(p);
        break;
    case PAGE_SAVE:
        DrawSavePage(p);
        break;
    case PAGE_CONFIG:
        DrawConfigPage(p);
        break;
    case PAGE_SECRET:
        DrawSecretsPage(p);
        break;
    default:
        DrawCheatzPage(p);
        break;
    }
}

bool gWideAtOpen = false;

// The pause menu puts back the display it found: the play state's windows change with the wide view.
void CloseMenuDraw()
{
    MenuDraw_Close();
    if (gSettings.mWideView != gWideAtOpen)
    {
        World_ApplyWindows();
    }
}
} // namespace

void Pause_Open()
{
    gWideAtOpen = gSettings.mWideView;
    gPause = PauseData();
    gPause.mOpen = true;
    gPause.mPageCount = PageCount();
    gPause.mLastState = gState;
    StateChanged();
    MenuDraw_Open();
    Draw();
}

bool Pause_IsOpen()
{
    return gPause.mOpen;
}

void Pause_Close()
{
    gPause.mOpen = false;
    CloseMenuDraw();
}

void Pause_Reset()
{
    gState = 0;
}

namespace
{
// PauseState.Update
bool UpdateMenu()
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
            d.mFlashFrames = 0;
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
} // namespace

bool Pause_Update()
{
    if (!UpdateMenu())
    {
        CloseMenuDraw();
        return false;
    }
    gPause.mFlashFrames++;
    gPause.mSelFrames++;
    Draw();
    return true;
}
