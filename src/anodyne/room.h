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
// The C#'s UI is 160 wide, centred: its x 0 is this far right of the playfield's left (0 with normal rooms).
constexpr int kUiX = (240 - 160) / 2 - kRoomViewX;

// The current map's tables (kRoomRemap[map]), and every map's (for Room_XIn/YIn).
void Room_SetRemap(const RoomRemapData& remap);
void Room_SetTables(const RoomRemapData* remaps, const char* const* names, int count);

#ifdef ANODYNE_WIDE_ROOMS
fx Room_X(int gx, fx local160);
fx Room_Y(int gy, fx local160);
fx Room_GridX160(fx worldX); // a world x, as an x in its 160px room
fx Room_GridY160(fx worldY);
fx Room_LocalX160(int gx, fx worldX); // the same against room gx (wherever the x is)
fx Room_LocalY160(int gy, fx worldY);
fx Room_XIn(const char* map, int gx, fx local160); // Room_X with another map's tables
fx Room_YIn(const char* map, int gy, fx local160);
#else
inline fx Room_X(int gx, fx local160)
{
    return Fx(gx * kRoomW) + local160;
}
inline fx Room_Y(int gy, fx local160)
{
    return Fx(gy * kRoomH) + local160;
}
// (MapUtilities.GetInGridPosition: (int) truncates)
inline fx Room_GridX160(fx worldX)
{
    return worldX - Fx(FxTrunc(worldX / kRoomW) * kRoomW);
}
inline fx Room_GridY160(fx worldY)
{
    return worldY - Fx(FxTrunc(worldY / kRoomH) * kRoomH);
}
inline fx Room_LocalX160(int gx, fx worldX)
{
    return worldX - Fx(gx * kRoomW);
}
inline fx Room_LocalY160(int gy, fx worldY)
{
    return worldY - Fx(gy * kRoomH);
}
inline fx Room_XIn(const char*, int gx, fx local160)
{
    return Room_X(gx, local160);
}
inline fx Room_YIn(const char*, int gy, fx local160)
{
    return Room_Y(gy, local160);
}
#endif

// The room (grid column / row) a world x / y is in.
inline int Room_GridOfX(fx worldX)
{
    return FxTrunc(worldX / kRoomW);
}
inline int Room_GridOfY(fx worldY)
{
    return FxTrunc(worldY / kRoomH);
}

// A room's left (kX) or top edge plus an offset written for the 160x160 rooms, standing in for the fx the code adds
// offsets to: edge + a + b lands where remap.py moved the spot a + b in from the edge (the C#'s RoomFrame). With
// normal rooms it's the same integer adds as the fx it replaces.
template <bool kX>
struct RoomEdge final
{
    int16_t mGrid;
    fx mOff;

    fx At(fx v) const
    {
        return kX ? Room_X(mGrid, mOff + v) : Room_Y(mGrid, mOff + v);
    }
    operator fx() const
    {
        return At(Fx(0));
    }
    friend RoomEdge operator+(RoomEdge e, fx v)
    {
        e.mOff = e.mOff + v;
        return e;
    }
    friend RoomEdge operator-(RoomEdge e, fx v)
    {
        e.mOff = e.mOff - v;
        return e;
    }
    friend fx operator+(fx v, RoomEdge e)
    {
        return e.At(v);
    }
    friend fx operator-(RoomEdge a, RoomEdge b)
    {
        return a.At(Fx(0)) - b.At(Fx(0));
    }
};
using RoomEdgeX = RoomEdge<true>;
using RoomEdgeY = RoomEdge<false>;

inline RoomEdgeX Room_Left(int gx)
{
    return { static_cast<int16_t>(gx), Fx(0) };
}
inline RoomEdgeY Room_Top(int gy)
{
    return { static_cast<int16_t>(gy), Fx(0) };
}
// The room a world x / y is in, as an edge (x - GetInGridPosition(x).X).
inline RoomEdgeX Room_LeftOf(fx worldX)
{
    return Room_Left(Room_GridOfX(worldX));
}
inline RoomEdgeY Room_TopOf(fx worldY)
{
    return Room_Top(Room_GridOfY(worldY));
}

// World_ChangeTile for tile (tx, ty) of room (gx, gy) as the 160x160 rooms had it: every tile of the wide room
// copied from it (a doubled column is two), or the one in its place for the dropped row.
void Room_ChangeTile160(int layer, int gx, int gy, int tx, int ty, int tile);

// A world position written for the 160x160 rooms (x of room x / 160 etc.).
inline fx Room_WorldX(int x160)
{
    return Room_X(x160 / 160, Fx(x160 % 160));
}
inline fx Room_WorldY(int y160)
{
    return Room_Y(y160 / 160, Fx(y160 % 160));
}
