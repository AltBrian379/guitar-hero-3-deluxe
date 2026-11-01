// Reads the in-game "P1/P2 Input Mode" setting (QScript globals dx_input_mode_device_0..3,
// see _qb/dx/dx_input_bridge.q) straight from GH3's global symbol table.
//
// Only enabled when the host is the GH3 PC v1.3 executable this was reverse-engineered
// against (checked by instruction signature); otherwise every query returns "no override".
#pragma once
#include <windows.h>

#include "xinput_types.h"

namespace uinput {

enum class GameInputMode { NoOverride = 0, Guitar = 1, Gamepad = 2 };

class GameBridge {
public:
    // Validates the executable. Call once, outside DllMain.
    void Init();
    bool Available() const { return available_; }
    // Re-reads the globals at most every 250 ms. Must be called on the game's input thread.
    void Refresh();
    GameInputMode ModeForSlot(DWORD slot) const { return slot < kMaxSlots ? slotMode_[slot] : GameInputMode::NoOverride; }

private:
    bool available_ = false;
    ULONGLONG lastRefresh_ = 0;
    GameInputMode slotMode_[kMaxSlots] = {};
    GameInputMode loggedMode_[kMaxSlots] = {};
};

}  // namespace uinput
