// PLACEHOLDERS. Replace these when you know how your game/payload exposes the real things.
// They are deliberately honest: ReadControllers says "no input", so the menu simply stays closed.
#include "integration.h"

bool Integration_ReadControllers(ControllerState&) { return false; }   // TODO: real controller input
void Integration_SetGameInputBlocked(bool) {}                          // TODO: stop clicks reaching the game
std::string Integration_DataDir() { return "/sdcard/Timmyzstuff/"; }   // TODO: confirm a writable folder
