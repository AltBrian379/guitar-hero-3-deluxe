#pragma once
#include <string>

#include "xinput_types.h"

namespace uinput {

// How a controller slot is presented to the game.
enum class SlotMode {
    Auto,     // guitars (any guitar subtype) -> GH guitar, everything else untouched
    Guitar,   // always present as a GH guitar
    Gamepad,  // always present as a regular gamepad
};

struct Config {
    bool enabled = true;
    bool log = true;
    SlotMode slotMode[kMaxSlots] = {SlotMode::Auto, SlotMode::Auto, SlotMode::Auto, SlotMode::Auto};
};

// Reads <iniPath>. Missing file/keys keep defaults.
Config LoadConfig(const std::wstring& iniPath);
const char* SlotModeName(SlotMode m);

}  // namespace uinput
