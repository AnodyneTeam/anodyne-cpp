#include "room.h"

#include "world.h"

#include <string.h>

namespace
{
const RoomRemapData* gRemap = nullptr;
const RoomRemapData* gRemaps = nullptr;
const char* const* gRemapNames = nullptr;
int gRemapCount = 0;

#ifdef ANODYNE_WIDE_ROOMS
// MapUtilities.RemapLocal's axis: an offset into a 160px room (g: its grid column/row) to one into the wide room.
fx Axis(fx v, const uint8_t* maps, uint32_t count, int g, int tiles)
{
    if (v < Fx(0))
    {
        return v;
    }
    if (v >= Fx(160))
    {
        return v - Fx(160) + Fx(tiles * 16);
    }
    const int tile = FxFloor(v / 16);
    const fx sub = v - Fx(tile * 16);
    if (!maps || g < 0 || g >= static_cast<int>(count))
    {
        return v * tiles / 10; // (no table: spread it out)
    }
    const uint8_t* src = maps + g * tiles;
    int i = -1;
    for (int k = 0; k < tiles && i < 0; k++)
    {
        i = src[k] == tile ? k : -1;
    }
    if (i < 0)
    {
        // (a dropped row: the row above it, or below for the top one)
        for (int k = 0; k < tiles && i < 0; k++)
        {
            i = src[k] == tile - 1 ? k : -1;
        }
        for (int k = 0; k < tiles && i < 0; k++)
        {
            i = src[k] == tile + 1 ? k : -1;
        }
    }
    else if (i + 1 < tiles && src[i + 1] == tile)
    {
        // a doubled column: the middle of the pair, so a thing centred on it stays centred
        return Fx(i * 16) + sub * 2;
    }
    return Fx(i * 16) + sub;
}

fx Unaxis(fx v, const uint8_t* maps, uint32_t count, int g, int tiles)
{
    if (!maps || g < 0 || g >= static_cast<int>(count))
    {
        return v * 10 / tiles;
    }
    const uint8_t* src = maps + g * tiles;
    int i = FxFloor(v / 16);
    i = i < 0 ? 0 : (i >= tiles ? tiles - 1 : i);
    const fx sub = v - Fx(i * 16);
    if (i + 1 < tiles && src[i + 1] == src[i])
    {
        return Fx(src[i] * 16) + sub / 2; // (first of a doubled pair)
    }
    if (i > 0 && src[i - 1] == src[i])
    {
        return Fx(src[i] * 16) + (sub + Fx(16)) / 2;
    }
    return Fx(src[i] * 16) + sub;
}
#endif
} // namespace

// The tiles (world tile coordinates) copied from tile t of a 160px room's column/row g: one, or two for a doubled
// column; the dropped row gives the one in its place.
int Room_Tiles160(int g, int t, bool x, int out[2])
{
#ifdef ANODYNE_WIDE_ROOMS
    const int tiles = x ? kRoomTilesW : kRoomTilesH;
    const uint8_t* maps = gRemap ? (x ? gRemap->mCols : gRemap->mRows) : nullptr;
    const uint32_t count = gRemap ? (x ? gRemap->mRoomsW : gRemap->mRoomsH) : 0;
    int n = 0;
    if (maps && g >= 0 && g < static_cast<int>(count))
    {
        for (int k = 0; k < tiles && n < 2; k++)
        {
            if (maps[g * tiles + k] == t)
            {
                out[n++] = g * tiles + k;
            }
        }
    }
    if (n == 0)
    {
        out[n++] = FxFloor(x ? Room_X(g, Fx(t * 16 + 8)) : Room_Y(g, Fx(t * 16 + 8))) / 16;
    }
    return n;
#else
    out[0] = g * (x ? kRoomTilesW : kRoomTilesH) + t;
    return 1;
#endif
}

void Room_ChangeTile160(int layer, int gx, int gy, int tx, int ty, int tile)
{
    int xs[2];
    int ys[2];
    const int nx = Room_Tiles160(gx, tx, true, xs);
    const int ny = Room_Tiles160(gy, ty, false, ys);
    for (int j = 0; j < ny; j++)
    {
        for (int i = 0; i < nx; i++)
        {
            World_ChangeTile(layer, xs[i], ys[j], tile);
        }
    }
}

void Room_SetRemap(const RoomRemapData& remap)
{
    gRemap = &remap;
}

void Room_SetTables(const RoomRemapData* remaps, const char* const* names, int count)
{
    gRemaps = remaps;
    gRemapNames = names;
    gRemapCount = count;
}

#ifdef ANODYNE_WIDE_ROOMS
fx Room_X(int gx, fx local160)
{
    return Fx(gx * kRoomW) + Axis(local160, gRemap ? gRemap->mCols : nullptr, gRemap ? gRemap->mRoomsW : 0, gx,
                                  kRoomTilesW);
}

fx Room_Y(int gy, fx local160)
{
    return Fx(gy * kRoomH) + Axis(local160, gRemap ? gRemap->mRows : nullptr, gRemap ? gRemap->mRoomsH : 0, gy,
                                  kRoomTilesH);
}

fx Room_GridX160(fx worldX)
{
    const int gx = FxTrunc(worldX / kRoomW);
    return Unaxis(worldX - Fx(gx * kRoomW), gRemap ? gRemap->mCols : nullptr, gRemap ? gRemap->mRoomsW : 0, gx,
                  kRoomTilesW);
}

fx Room_GridY160(fx worldY)
{
    const int gy = FxTrunc(worldY / kRoomH);
    return Unaxis(worldY - Fx(gy * kRoomH), gRemap ? gRemap->mRows : nullptr, gRemap ? gRemap->mRoomsH : 0, gy,
                  kRoomTilesH);
}

fx Room_LocalX160(int gx, fx worldX)
{
    return Unaxis(worldX - Fx(gx * kRoomW), gRemap ? gRemap->mCols : nullptr, gRemap ? gRemap->mRoomsW : 0, gx,
                  kRoomTilesW);
}

fx Room_LocalY160(int gy, fx worldY)
{
    return Unaxis(worldY - Fx(gy * kRoomH), gRemap ? gRemap->mRows : nullptr, gRemap ? gRemap->mRoomsH : 0, gy,
                  kRoomTilesH);
}

namespace
{
const RoomRemapData* RemapOf(const char* map)
{
    for (int i = 0; i < gRemapCount; i++)
    {
        if (strcmp(gRemapNames[i], map) == 0)
        {
            return &gRemaps[i];
        }
    }
    return gRemap;
}
} // namespace

fx Room_XIn(const char* map, int gx, fx local160)
{
    const RoomRemapData* current = gRemap;
    gRemap = RemapOf(map);
    const fx x = Room_X(gx, local160);
    gRemap = current;
    return x;
}

fx Room_YIn(const char* map, int gy, fx local160)
{
    const RoomRemapData* current = gRemap;
    gRemap = RemapOf(map);
    const fx y = Room_Y(gy, local160);
    gRemap = current;
    return y;
}
#endif
