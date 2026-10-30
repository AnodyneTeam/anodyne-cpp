#pragma once

// The size of a room (a screen of the map) and, with wide rooms, where the code's own positions in a room went.
//
// Normal rooms are AnodyneSharp's 10x10 tiles (160x160, centred on the 240x160 screen with the HUD in the side bars).
// Wide rooms (cmake -DANODYNE_WIDE_ROOMS=ON) are 15x9 tiles (240x144, the whole screen under a 16px HUD row), from
// old/ContentWide (tools/wide_rooms/remap.py: five columns of each room drawn twice and one row left out). Positions
// the code has of its own were written for the 160x160 rooms: Room_X/Room_Y move one to where remap.py moved that
// spot, and Room_GridX160/Room_GridY160 go back, for checks written against the old numbers. With normal rooms they
// are the same sums as before.

#include "ecs.h"
#include "map_data.h"

#ifdef ANODYNE_WIDE_ROOMS
constexpr bool kWideRooms = true;
constexpr int kRoomW = 240;
constexpr int kRoomH = 144;
constexpr int kHeaderH = 16; // GameConstants.HEADER_HEIGHT
#else
constexpr bool kWideRooms = false;
constexpr int kRoomW = 160; // SCREEN_WIDTH_IN_PIXELS
constexpr int kRoomH = 160; // SCREEN_HEIGHT_IN_PIXELS
constexpr int kHeaderH = 20;
#endif
constexpr int kRoomTilesW = kRoomW / 16;
constexpr int kRoomTilesH = kRoomH / 16;
constexpr int kRoomViewX = (240 - kRoomW) / 2; // the playfield's top-left on the GBA screen
constexpr int kRoomViewY = 160 - kRoomH;

// The current map's tables (kRoomRemap[map]).
void Room_SetRemap(const RoomRemapData& remap);

#ifdef ANODYNE_WIDE_ROOMS
fx Room_X(int gx, fx local160);
fx Room_Y(int gy, fx local160);
fx Room_GridX160(fx worldX); // a world x, as an x in its 160px room
fx Room_GridY160(fx worldY);
#else
inline fx Room_X(int gx, fx local160)
{
    return Fx(gx * kRoomW) + local160;
}
inline fx Room_Y(int gy, fx local160)
{
    return Fx(gy * kRoomH) + local160;
}
inline fx Room_GridX160(fx worldX)
{
    return worldX - Fx(FxFloor(worldX / kRoomW) * kRoomW);
}
inline fx Room_GridY160(fx worldY)
{
    return worldY - Fx(FxFloor(worldY / kRoomH) * kRoomH);
}
#endif

// A world position written for the 160x160 rooms (x of room x / 160 etc.).
inline fx Room_WorldX(int x160)
{
    return Room_X(x160 / 160, Fx(x160 % 160));
}
inline fx Room_WorldY(int y160)
{
    return Room_Y(y160 / 160, Fx(y160 % 160));
}
