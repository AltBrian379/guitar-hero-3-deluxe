#include "real_xinput.h"

#include <cwchar>

#include "log.h"

namespace uinput {

bool LoadRealXInput(RealXInput& out) {
    // GetSystemDirectory returns System32; for a 32-bit process WOW64 redirects that to SysWOW64.
    wchar_t sysDir[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    static const wchar_t* kCandidates[] = {L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* name : kCandidates) {
        swprintf_s(out.path, L"%s\\%s", sysDir, name);
        out.module = LoadLibraryW(out.path);
        if (out.module) break;
        Log("could not load %ls (error %lu)", out.path, GetLastError());
    }
    if (!out.module) return false;

    auto get = [&](const char* name) { return GetProcAddress(out.module, name); };
    out.getState = reinterpret_cast<RealXInput::GetStateFn>(get("XInputGetState"));
    out.getStateEx = reinterpret_cast<RealXInput::GetStateFn>(get(MAKEINTRESOURCEA(100)));
    out.setState = reinterpret_cast<RealXInput::SetStateFn>(get("XInputSetState"));
    out.getCapabilities = reinterpret_cast<RealXInput::GetCapabilitiesFn>(get("XInputGetCapabilities"));
    out.enable = reinterpret_cast<RealXInput::EnableFn>(get("XInputEnable"));
    out.getDSoundGuids = reinterpret_cast<RealXInput::GetDSoundGuidsFn>(get("XInputGetDSoundAudioDeviceGuids"));
    out.getBattery = reinterpret_cast<RealXInput::GetBatteryFn>(get("XInputGetBatteryInformation"));
    out.getKeystroke = reinterpret_cast<RealXInput::GetKeystrokeFn>(get("XInputGetKeystroke"));

    Log("real XInput: %ls (GetState=%p GetCapabilities=%p Enable=%p)", out.path, out.getState,
        out.getCapabilities, out.enable);
    return out.getState && out.getCapabilities;
}

}  // namespace uinput
