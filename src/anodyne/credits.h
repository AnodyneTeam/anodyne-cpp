#pragma once

// CreditsState (States/CreditsState.cs): the ending's scrolling credits and the save popup after them.

// GlobalState.GameState.SetState<CreditsState>() from inside the play state: the switch happens after its frame.
void Credits_Request();
// Takes the request (true once).
bool Credits_TakeRequest();

// Sets the screen up for the credits (the play state's effects are deactivated).
void Credits_Enter();
// One frame. Returns true when the popup has been answered (SetState<TitleState>).
bool Credits_Update();
// Hides what the credits drew.
void Credits_Exit();
