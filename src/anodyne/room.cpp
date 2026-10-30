#include "room.h"

namespace
{
const RoomRemapData* gRemap = nullptr;

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

void Room_SetRemap(const RoomRemapData& remap)
{
    gRemap = &remap;
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
    const int gx = FxFloor(worldX / kRoomW);
    return Unaxis(worldX - Fx(gx * kRoomW), gRemap ? gRemap->mCols : nullptr, gRemap ? gRemap->mRoomsW : 0, gx,
                  kRoomTilesW);
}

fx Room_GridY160(fx worldY)
{
    const int gy = FxFloor(worldY / kRoomH);
    return Unaxis(worldY - Fx(gy * kRoomH), gRemap ? gRemap->mRows : nullptr, gRemap ? gRemap->mRoomsH : 0, gy,
                  kRoomTilesH);
}
#endif
