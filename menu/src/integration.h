// integration.h - THE THREE THINGS ONLY YOUR GAME SETUP CAN ANSWER.
// The menu code never talks to the game directly. It calls these functions instead.
// Right now they are empty placeholders (see integration_stubs.cpp) so nothing is faked.
#pragma once
#include <string>
#include "menu_input.h"

// 1) Read the controllers. Fill `out` and return true. Return false if you can't (menu then never opens).
bool Integration_ReadControllers(ControllerState& out);

// 2) The menu opened (true) or closed (false). Use this to stop the game reacting to your
//    clicks while the menu is up (otherwise clicks may press game buttons behind it).
void Integration_SetGameInputBlocked(bool blocked);

// 3) A folder the game may write to, WITH a trailing slash. Colours + settings are saved here.
std::string Integration_DataDir();

// Where the menu floats is also up to the integration: use MenuSettings::menuDistance (metres-ish, 0.5-3.0).
