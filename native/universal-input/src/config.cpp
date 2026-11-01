#include "config.h"

#include <windows.h>

#include <cwchar>

namespace uinput {
namespace {

bool ReadBool(const std::wstring& ini, const wchar_t* section, const wchar_t* key, bool def) {
    return GetPrivateProfileIntW(section, key, def ? 1 : 0, ini.c_str()) != 0;
}

SlotMode ParseMode(const wchar_t* s, SlotMode def) {
    if (_wcsicmp(s, L"auto") == 0) return SlotMode::Auto;
    if (_wcsicmp(s, L"guitar") == 0) return SlotMode::Guitar;
    if (_wcsicmp(s, L"gamepad") == 0) return SlotMode::Gamepad;
    return def;
}

}  // namespace

Config LoadConfig(const std::wstring& iniPath) {
    Config c;
    c.enabled = ReadBool(iniPath, L"General", L"Enabled", c.enabled);
    c.log = ReadBool(iniPath, L"General", L"Log", c.log);
    c.hideFromDirectInput = ReadBool(iniPath, L"General", L"HideFromDirectInput", c.hideFromDirectInput);
    for (DWORD i = 0; i < kMaxSlots; ++i) {
        wchar_t section[16];
        swprintf_s(section, L"Slot%lu", i + 1);
        wchar_t value[32] = {};
        GetPrivateProfileStringW(section, L"Mode", L"Auto", value, 32, iniPath.c_str());
        c.slotMode[i] = ParseMode(value, SlotMode::Auto);
    }
    return c;
}

const char* SlotModeName(SlotMode m) {
    switch (m) {
        case SlotMode::Auto: return "Auto";
        case SlotMode::Guitar: return "Guitar";
        case SlotMode::Gamepad: return "Gamepad";
    }
    return "?";
}

}  // namespace uinput
