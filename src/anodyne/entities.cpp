#include "entities.h"

#include "sprite.h"

namespace
{
constexpr int kFirstOam = 10;
constexpr int kOamEnd = 128;
constexpr int kFirstPalBank = 4;
constexpr int kPalBankCount = 12;
constexpr int kMaxSprites = 64;
constexpr int kScreenPx = 160;

ObjVramAllocator* gVram = nullptr;
MapEntityList gList = { 0, nullptr };
const char* const* gSheetNames = nullptr;
uint32_t gSheetCount = 0;

Sprite gSprites[kMaxSprites];
int16_t gSpriteX[kMaxSprites];
int16_t gSpriteY[kMaxSprites];
int gSpriteCount = 0;
int gShownGrid = -1; // gridY * 256 + gridX of the sprites that exist, -1 = none

void DestroyAll()
{
    for (int i = 0; i < gSpriteCount; i++)
    {
        Sprite_Hide(gSprites[i]);
        Sprite_Destroy(gSprites[i], *gVram);
    }
    gSpriteCount = 0;
    gShownGrid = -1;
}

void Rebuild(int gridX, int gridY)
{
    DestroyAll();
    gShownGrid = gridY * 256 + gridX;

    // One palette bank per distinct sheet on this screen.
    const SpriteSheetEntry* bankSheets[kPalBankCount] = {};
    int oamNext = kFirstOam;

    for (uint32_t i = 0; i < gList.mCount && gSpriteCount < kMaxSprites; i++)
    {
        const MapEntity& e = gList.mEntities[i];
        if (e.mX / kScreenPx != gridX || e.mY / kScreenPx != gridY || e.mSheet >= gSheetCount)
        {
            continue;
        }
        const SpriteSheetEntry* sheet = Sprite_FindSheet(gSheetNames[e.mSheet]);
        if (!sheet || !Sprite_IsDrawable(*sheet))
        {
            continue;
        }
        int bank = -1;
        for (int b = 0; b < kPalBankCount; b++)
        {
            if (bankSheets[b] == sheet)
            {
                bank = b;
                break;
            }
            if (!bankSheets[b] && bank < 0)
            {
                bank = b;
            }
        }
        if (bank < 0 || oamNext + static_cast<int>(sheet->mPartCount) > kOamEnd)
        {
            continue;
        }

        Sprite& sprite = gSprites[gSpriteCount];
        if (!Sprite_Create(sprite, *gVram, gSheetNames[e.mSheet], kFirstPalBank + bank, oamNext))
        {
            continue;
        }
        bankSheets[bank] = sheet;
        oamNext += Sprite_OamCount(sprite);
        Sprite_SetFrame(sprite, sheet->mFrameCount ? e.mFrame % sheet->mFrameCount : 0);
        Sprite_Update(sprite);
        gSpriteX[gSpriteCount] = e.mX;
        gSpriteY[gSpriteCount] = e.mY;
        gSpriteCount++;
    }
}
} // namespace

void Entities_Init(ObjVramAllocator& vram)
{
    gVram = &vram;
    gSpriteCount = 0;
    gShownGrid = -1;
}

void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount)
{
    if (gVram)
    {
        DestroyAll();
    }
    gList = list;
    gSheetNames = sheetNames;
    gSheetCount = sheetCount;
}

void Entities_Clear()
{
    if (gVram)
    {
        DestroyAll();
    }
}

void Entities_Update(int gridX, int gridY, int camX, int camY, int screenX)
{
    if (!gVram)
    {
        return;
    }
    if (gShownGrid != gridY * 256 + gridX)
    {
        Rebuild(gridX, gridY);
    }
    for (int i = 0; i < gSpriteCount; i++)
    {
        Sprite_Update(gSprites[i]);
        Sprite_Draw(gSprites[i], gSpriteX[i] - camX + screenX, gSpriteY[i] - camY);
    }
}
