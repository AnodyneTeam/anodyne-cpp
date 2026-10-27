#pragma once

#include <stdint.h>

// ---- Minimap (MinimapTracker): which screens of each map have been seen, and the unopened chests on them ---------
// PlayState.UpdateScreenBorders: the screen the player is on is seen (any chests count again as it loads).
void Minimap_Visit(int gx, int gy);
// Minimap.AddInterest / RemoveInterest (delta -1): a chest on screen (gx, gy) of the current map.
void Minimap_AddInterest(int gx, int gy, int delta);

// ---- The pause menu's picture --------------------------------------------------------------------------------------
// The menu draws into an 8bpp bitmap BG over the playfield (the map's tileset VRAM and palette, given back when it
// closes). Each frame pause.cpp lists what to draw (MenuDraw_*); the bitmap is only redrawn when the list changes.
// Coordinates are the C#'s UI ones: the playfield starts at y = 20 (HEADER_HEIGHT).

enum MenuImage : uint8_t
{
    MENU_IMG_BG,
    MENU_IMG_PAGE_SELECTOR, // menu_select_active: flash 0, 1; inactive 2
    MENU_IMG_ARROWS,        // MenuSelector: disabledLeft 0, enabledLeft 0 1, disabledRight 2, enabledRight 3 2
    MENU_IMG_MINIMAP_TILES,
    MENU_IMG_NONE_ICON,
    MENU_IMG_LONG_ICON,
    MENU_IMG_WIDE_ICON,
    MENU_IMG_TRANSFORMER_ICON,
    MENU_IMG_EQUIPPED_ICON,
    MENU_IMG_JUMP_SHOES,
    MENU_IMG_TRADE_ITEMS, // the shoes, the box
    MENU_IMG_BIG_KEYS,
    MENU_IMG_CARDS,
    MENU_IMG_CHECKBOX,
    MENU_IMG_VOLUME_BAR,
    MENU_IMG_POP_MENU,
    MENU_IMG_CONTROLS_BOX,
    MENU_IMG_SECRETS,
    MENU_IMG_COUNT,
};

// The page's palette group (the colours every page shares, plus the page's own).
enum MenuGroup : uint8_t
{
    MENU_GROUP_NONE,
    MENU_GROUP_MAP,
    MENU_GROUP_EQUIP,
    MENU_GROUP_CARDS,
    MENU_GROUP_CONFIG,
    MENU_GROUP_SECRETS,
};

enum MenuColour : uint8_t
{
    MENU_WHITE,
    MENU_BLACK,
    MENU_GREY,
    MENU_RED,
    MENU_VOLUME_INSIDE, // (config group)
    MENU_VOLUME_BG,
};

// The menu opens / closes: takes over the display, and gives it back.
void MenuDraw_Open();
void MenuDraw_Close();
bool MenuDraw_IsOpen();
// Starts this frame's list (with the page's palette group).
void MenuDraw_Begin(MenuGroup group);
void MenuDraw_Image(MenuImage image, int frame, int x, int y);
// A UILabel: white with a black shadow 1px below unless told otherwise; '\n' starts a new line 10px down.
void MenuDraw_Text(const char* text, int x, int y, MenuColour colour = MENU_WHITE, bool shadow = true);
void MenuDraw_Fill(int x, int y, int w, int h, MenuColour colour);
// The current map's minimap (Minimap.Draw) with its top-left tile at (x, y); false if the map has none.
bool MenuDraw_Minimap(int centreX, int centreY, bool playerIndicator);
// Width of a label's longest line in px.
int MenuDraw_TextWidth(const char* text);
// After the world has drawn: the menu's registers over the world's, and the bitmap if the list changed.
void MenuDraw_Present();
