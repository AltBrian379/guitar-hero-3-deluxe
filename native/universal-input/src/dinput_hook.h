// Hides controllers we remap from GH3's DirectInput enumeration, so a mapped HID guitar
// does not show up twice (once as our virtual XInput guitar, once as a DirectInput gamepad).
#pragma once
#include <windows.h>

namespace uinput {

using HideDevicePredicate = bool (*)(USHORT vid, USHORT pid, const char* productName);

// Patches the host executable's import of dinput8!DirectInput8Create. Safe to call from
// DllMain (no library loading). onFirstUse runs (outside DllMain) when the game first creates
// DirectInput. Returns false if the executable doesn't import it.
bool InstallDirectInputHook(HideDevicePredicate shouldHide, void (*onFirstUse)());

}  // namespace uinput
