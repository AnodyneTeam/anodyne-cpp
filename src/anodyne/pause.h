#pragma once

// The pause menu (PauseState and its pages: MapSubstate, EquipSubstate, CardSubstate, SaveSubstate, ConfigSubstate,
// SecretSubstate, CheatzSubstate), ported from AnodyneSharp. It is one of PlayState's child states: while it is up
// nothing else updates (UpdateEntities = false).

// PlayState.StateNormal's Pause press: SetSubstate(new PauseState()). The caller plays "pause_sound".
void Pause_Open();
bool Pause_IsOpen();
// PauseState.Update, once a frame while open. False once it has exited (PlayState drops it the same frame).
bool Pause_Update();
// Shuts the menu (its play state is going).
void Pause_Close();
// PauseState.Reset: the menu opens on the map page again (a new game or a loaded one).
void Pause_Reset();
