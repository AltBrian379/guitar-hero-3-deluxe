// The system's genuine XInput DLL, loaded by full path so we never load ourselves.
#pragma once
#include "xinput_types.h"

namespace uinput {

struct RealXInput {
    using GetStateFn = DWORD(WINAPI*)(DWORD, XState*);
    using SetStateFn = DWORD(WINAPI*)(DWORD, XVibration*);
    using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XCapabilities*);
    using EnableFn = void(WINAPI*)(BOOL);
    using GetDSoundGuidsFn = DWORD(WINAPI*)(DWORD, GUID*, GUID*);
    using GetBatteryFn = DWORD(WINAPI*)(DWORD, BYTE, XBatteryInformation*);
    using GetKeystrokeFn = DWORD(WINAPI*)(DWORD, DWORD, XKeystroke*);

    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = {};
    GetStateFn getState = nullptr;
    GetStateFn getStateEx = nullptr;  // ordinal 100: same as GetState but includes the Guide button
    SetStateFn setState = nullptr;
    GetCapabilitiesFn getCapabilities = nullptr;
    EnableFn enable = nullptr;
    GetDSoundGuidsFn getDSoundGuids = nullptr;
    GetBatteryFn getBattery = nullptr;
    GetKeystrokeFn getKeystroke = nullptr;
};

// Loads the real DLL (xinput1_3, falling back to 1_4 / 9_1_0). Returns false if none load.
bool LoadRealXInput(RealXInput& out);

}  // namespace uinput
