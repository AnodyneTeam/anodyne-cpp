// Gadgets and events (AnodyneSharp Entities/Gadget, Entities/Events).
#include "entity_classes.h"

#include <stdlib.h>

namespace
{
// ---- Console (Console.cs) -------------------------------------------------------------------------------------
void ConsoleCollided(Entity& self, Entity& other)
{
    Ecs_Separate(other, self);
}

bool ConsoleInteract(Entity& self, Facing)
{
    if (Preset_Activated(*self.mPreset))
    {
        return false;
    }
    Preset_SetActivated(*self.mPreset, true);
    Ecs_Play(self, "green");
    gGame.mPuzzlesSolved++;
    Game_PlaySfx("get_small_health");
    return true;
}

const EntityClass kConsole = {
    "Console", nullptr, nullptr, ConsoleCollided, ConsoleInteract, nullptr, nullptr, nullptr,
    0,         GROUP_PLAYER, ECF_INTERACTABLE,
};

// ---- Gate (Gate.cs) -------------------------------------------------------------------------------------------
struct GateData final
{
    bool mHeldDown; // the player stood on it when the screen was entered
};

bool GateCondition(const MapEntity& p)
{
    // 0-4, 10-14: enemies killed; 5-7, 15-17: puzzles solved
    const int check = (p.mFrame % 10 >= 5) ? gGame.mPuzzlesSolved : gGame.mEnemiesKilled;
    return check > p.mFrame % 5;
}

void GateUpdate(Entity& self)
{
    GateData& d = self.Data<GateData>();
    const MapEntity& p = *self.mPreset;
    if (d.mHeldDown)
    {
        if (!Ecs_Intersects(Ecs_Hitbox(*World_Player()), Ecs_Hitbox(self)))
        {
            d.mHeldDown = false;
            Ecs_Play(self, "close");
            Game_PlaySfx("hit_ground_1");
            Ecs_SetSolid(self, true);
        }
    }
    else if (Ecs_AnimIs(self, "still") || Ecs_AnimIs(self, "close"))
    {
        if (GateCondition(p))
        {
            Ecs_Play(self, "open");
            Game_PlaySfx("open");
            if (p.mFrame < 10)
            {
                Preset_SetAlive(p, false);
            }
            Ecs_SetSolid(self, false);
        }
    }
    Ecs_BaseUpdate(self);
}

void GateCollided(Entity& self, Entity& other)
{
    if (Ecs_Solid(self))
    {
        Ecs_Separate(self, other);
    }
}

const EntityClass kGate = {
    "Gate", GateUpdate, nullptr, GateCollided, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_PART_OF_MAP,
};

// ---- SageBlank (Sage.cs): BLANK's invisible Sage talks when the camera arrives on its screen ---------------------
void SageBlankEvent(Entity& self, int ev)
{
    if (ev != GEV_END_SCREEN_TRANSITION || !self.mExists)
    {
        return;
    }
    Game_ShowDialogue("sage", "intro", self.mPreset->mFrame + 1);
    self.mExists = false;
    Preset_SetAlive(*self.mPreset, false);
}

const EntityClass kSageBlank = {
    "SageBlank", nullptr, nullptr, nullptr, nullptr, nullptr, SageBlankEvent, nullptr, 0, 0, 0,
};
} // namespace

Entity* Spawn_Console(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kConsole, preset, "console");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    if (Preset_Activated(preset))
    {
        Ecs_Play(*e, "green");
        gGame.mPuzzlesSolved++;
    }
    else
    {
        Ecs_Play(*e, "active");
    }
    return e;
}

Entity* Spawn_Gate(const MapEntity& preset, Entity& player)
{
    Entity* e = Spawn_Basic(kGate, preset, "gates");
    if (!e)
    {
        return nullptr;
    }
    const int closed = atoi(Preset_TypeValue(preset));
    Ecs_SetFrameBase(*e, closed);
    e->mB->mImmovable = true;
    GateData& d = e->Data<GateData>();
    d.mHeldDown = Ecs_Intersects(Ecs_Hitbox(player), Ecs_Hitbox(*e));
    // The C# builds "close"/"open" at 10 fps for the first gate graphics (frame 0), else 8 / 4.
    if (d.mHeldDown && !GateCondition(preset))
    {
        Ecs_Play(*e, "opened");
        Ecs_SetSolid(*e, false);
    }
    else
    {
        Ecs_Play(*e, "still");
    }
    if (closed == 0)
    {
        e->mV->mFpsOverride = 10;
    }
    return e;
}

Entity* Spawn_SageBlank(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kSageBlank, Fx(preset.mX), Fx(preset.mY), 0, &preset);
    if (e)
    {
        e->mB->mW = 16;
        e->mB->mH = 16;
        Ecs_SetSolid(*e, false);
    }
    return e;
}
