// xinput1_3.dll proxy for Guitar Hero III (PC).
//
// GH3 imports XInput 1.3 by ordinal (#2 GetState, #4 GetCapabilities, #5 Enable) and only
// treats a controller as a guitar when XInputGetCapabilities reports
// SubType == XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE (7). Everything else, including Rock Band
// style guitars (6) and bass guitars (0x0B), is handled as a regular gamepad.
//
// This proxy forwards every call to the system's real XInput and only rewrites the reported
// SubType according to the per-slot mode in gh3_universal_input.ini:
//   Auto    - any guitar subtype -> 7, everything else untouched (regular gamepads are unaffected)
//   Guitar  - always 7
//   Gamepad - always 1 (regular gamepad)

#include <string>

#include "config.h"
#include "log.h"
#include "real_xinput.h"

namespace uinput {
namespace {

INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
Config g_config;
RealXInput g_real;
bool g_ready = false;

// Last logged state per slot, so the log records changes instead of every poll.
struct SlotLogState {
    bool connected = false;
    BYTE realSubType = 0;
    BYTE reportedSubType = 0;
};
SlotLogState g_slotLog[kMaxSlots];
SRWLOCK g_slotLogLock = SRWLOCK_INIT;

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*) {
    const std::wstring dir = ExeDirectory();
    g_config = LoadConfig(dir + L"\\gh3_universal_input.ini");
    if (g_config.log) LogOpen((dir + L"\\gh3_universal_input.log").c_str());
    Log("GH3 Universal Input starting (enabled=%d)", g_config.enabled ? 1 : 0);
    for (DWORD i = 0; i < kMaxSlots; ++i) Log("  slot %lu mode: %s", i + 1, SlotModeName(g_config.slotMode[i]));
    g_ready = LoadRealXInput(g_real);
    if (!g_ready) Log("ERROR: no system XInput DLL could be loaded; all controllers will read as disconnected");
    return TRUE;
}

void EnsureInit() { InitOnceExecuteOnce(&g_initOnce, InitOnce, nullptr, nullptr); }

bool IsGuitarSubType(BYTE subType) {
    return subType == kSubGuitar || subType == kSubGuitarAlternate || subType == kSubGuitarBass;
}

BYTE ResolveSubType(DWORD slot, BYTE realSubType) {
    if (!g_config.enabled) return realSubType;
    switch (g_config.slotMode[slot]) {
        case SlotMode::Guitar: return kSubGuitarAlternate;
        case SlotMode::Gamepad: return kSubGamepad;
        case SlotMode::Auto: break;
    }
    return IsGuitarSubType(realSubType) ? kSubGuitarAlternate : realSubType;
}

void NoteSlot(DWORD slot, bool connected, BYTE realSubType, BYTE reportedSubType) {
    AcquireSRWLockExclusive(&g_slotLogLock);
    SlotLogState& s = g_slotLog[slot];
    if (s.connected != connected || s.realSubType != realSubType || s.reportedSubType != reportedSubType) {
        if (connected) {
            Log("slot %lu: connected, real subtype %u -> reported %u (%s)", slot + 1, realSubType, reportedSubType,
                reportedSubType == kSubGuitarAlternate ? "guitar" : "not a guitar to GH3");
        } else if (s.connected) {
            Log("slot %lu: disconnected", slot + 1);
        }
        s = {connected, realSubType, reportedSubType};
    }
    ReleaseSRWLockExclusive(&g_slotLogLock);
}

}  // namespace
}  // namespace uinput

using namespace uinput;

extern "C" {

DWORD WINAPI Proxy_XInputGetCapabilities(DWORD userIndex, DWORD flags, XCapabilities* caps) {
    EnsureInit();
    if (!caps || userIndex >= kMaxSlots) return ERROR_BAD_ARGUMENTS;
    if (!g_ready) return ERROR_DEVICE_NOT_CONNECTED;

    // Query without XINPUT_FLAG_GAMEPAD so non-gamepad devices are never filtered out by the
    // system DLL; the subtype we report is decided by ResolveSubType instead.
    (void)flags;
    XCapabilities real = {};
    DWORD result = g_real.getCapabilities(userIndex, 0, &real);
    if (result != ERROR_SUCCESS) {
        NoteSlot(userIndex, false, 0, 0);
        return result;
    }
    const BYTE realSubType = real.SubType;
    real.SubType = ResolveSubType(userIndex, realSubType);
    NoteSlot(userIndex, true, realSubType, real.SubType);
    *caps = real;
    return ERROR_SUCCESS;
}

DWORD WINAPI Proxy_XInputGetState(DWORD userIndex, XState* state) {
    EnsureInit();
    if (!state || userIndex >= kMaxSlots) return ERROR_BAD_ARGUMENTS;
    if (!g_ready) return ERROR_DEVICE_NOT_CONNECTED;
    return g_real.getState(userIndex, state);
}

DWORD WINAPI Proxy_XInputGetStateEx(DWORD userIndex, XState* state) {
    EnsureInit();
    if (!state || userIndex >= kMaxSlots) return ERROR_BAD_ARGUMENTS;
    if (!g_ready) return ERROR_DEVICE_NOT_CONNECTED;
    return g_real.getStateEx ? g_real.getStateEx(userIndex, state) : g_real.getState(userIndex, state);
}

DWORD WINAPI Proxy_XInputSetState(DWORD userIndex, XVibration* vibration) {
    EnsureInit();
    if (!g_ready || !g_real.setState) return ERROR_DEVICE_NOT_CONNECTED;
    return g_real.setState(userIndex, vibration);
}

void WINAPI Proxy_XInputEnable(BOOL enable) {
    EnsureInit();
    if (g_ready && g_real.enable) g_real.enable(enable);
}

DWORD WINAPI Proxy_XInputGetDSoundAudioDeviceGuids(DWORD userIndex, GUID* render, GUID* capture) {
    EnsureInit();
    if (!g_ready || !g_real.getDSoundGuids) return ERROR_DEVICE_NOT_CONNECTED;
    return g_real.getDSoundGuids(userIndex, render, capture);
}

DWORD WINAPI Proxy_XInputGetBatteryInformation(DWORD userIndex, BYTE devType, XBatteryInformation* info) {
    EnsureInit();
    if (!g_ready || !g_real.getBattery) return ERROR_DEVICE_NOT_CONNECTED;
    return g_real.getBattery(userIndex, devType, info);
}

DWORD WINAPI Proxy_XInputGetKeystroke(DWORD userIndex, DWORD reserved, XKeystroke* keystroke) {
    EnsureInit();
    if (!g_ready || !g_real.getKeystroke) return ERROR_EMPTY;
    return g_real.getKeystroke(userIndex, reserved, keystroke);
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    // Keep DllMain trivial (loader lock): all real work happens lazily in EnsureInit().
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
