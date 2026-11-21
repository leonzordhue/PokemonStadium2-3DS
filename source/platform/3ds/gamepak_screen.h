// Pokemon Stadium 2 (3DS) - Game Pak screen (P44). Runs on the game thread
// before the game starts; returns false if the bottom screen cannot be drawn
// (the caller then falls back to the settings menu's Game page, 0039).
#pragma once
bool gamepak_screen_run();
