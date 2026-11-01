// xinput1_3.dll proxy for Guitar Hero III (PC).
//
// GH3 imports XInput 1.3 by ordinal (#2 GetState, #4 GetCapabilities, #5 Enable) and only
// treats a controller as a guitar when XInputGetCapabilities reports
// SubType == XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE (7). Everything else, including Rock Band
// style guitars (6), bass guitars (0x0B) and every non-XInput controller, is a gamepad.
//
// This proxy:
//  * forwards real XInput controllers, rewriting the reported SubType per slot mode:
//      Auto    - any guitar subtype -> 7, everything else untouched (gamepads unaffected)
//      Guitar  - always 7
//      Gamepad - always 1 (regular gamepad)
//  * presents HID controllers that have a [Device.VVVV:PPPP] mapping (made with
//    gh3-guitar-setup.exe) as Xbox 360 GH guitars in free slots, and hides them from
//    GH3's DirectInput enumeration so they don't appear twice.

#include <string>

#include "config.h"
#include "dinput_hook.h"
#include "hid_manager.h"
#include "log.h"
#include "real_xinput.h"

namespace uinput {
namespace {

INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
Config g_config;
RealXInput g_real;
HidManager g_hid;
bool g_ready = false;
bool g_dinputHooked = false;

// Last logged state per slot, so the log records changes instead of every poll.
struct SlotLogState {
    bool connected = false;
    bool virtualDevice = false;
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
    const std::wstring ini = dir + L"\\gh3_universal_input.ini";
    g_config = LoadConfig(ini);
    if (g_config.log) LogOpen((dir + L"\\gh3_universal_input.log").c_str());
    Log("GH3 Universal Input starting (enabled=%d, hideFromDirectInput=%d, DirectInput hook %s)",
        g_config.enabled ? 1 : 0, g_config.hideFromDirectInput ? 1 : 0, g_dinputHooked ? "installed" : "not installed");
    for (DWORD i = 0; i < kMaxSlots; ++i) Log("  slot %lu mode: %s", i + 1, SlotModeName(g_config.slotMode[i]));
    g_ready = LoadRealXInput(g_real);
    if (!g_ready) Log("ERROR: no system XInput DLL could be loaded; real XInput controllers will read as disconnected");
    if (g_config.enabled) g_hid.Start(LoadProfiles(ini));
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

void NoteSlot(DWORD slot, bool connected, bool virtualDevice, BYTE realSubType, BYTE reportedSubType) {
    AcquireSRWLockExclusive(&g_slotLogLock);
    SlotLogState& s = g_slotLog[slot];
    if (s.connected != connected || s.virtualDevice != virtualDevice || s.realSubType != realSubType ||
        s.reportedSubType != reportedSubType) {
        const char* kind = reportedSubType == kSubGuitarAlternate ? "guitar" : "not a guitar to GH3";
        if (connected && virtualDevice) {
            Log("slot %lu: HID guitar \"%ls\" -> reported subtype %u (%s)", slot + 1, g_hid.NameOf(slot).c_str(),
                reportedSubType, kind);
        } else if (connected) {
            Log("slot %lu: connected, real subtype %u -> reported %u (%s)", slot + 1, realSubType, reportedSubType, kind);
        } else if (s.connected) {
            Log("slot %lu: disconnected", slot + 1);
        }
        s = {connected, virtualDevice, realSubType, reportedSubType};
    }
    ReleaseSRWLockExclusive(&g_slotLogLock);
}

void FillVirtualCapabilities(DWORD slot, XCapabilities* caps) {
    *caps = {};
    caps->Type = kDevTypeGamepad;
    caps->SubType = ResolveSubType(slot, kSubGuitarAlternate);
    caps->Gamepad.wButtons = kA | kB | kX | kY | kLShoulder | kDpadUp | kDpadDown | kStart | kBack;
    caps->Gamepad.sThumbRX = static_cast<SHORT>(0xFFC0);
    caps->Gamepad.sThumbRY = static_cast<SHORT>(0xFFC0);
}

bool ShouldHideFromDirectInput(USHORT vid, USHORT pid, const char*) {
    EnsureInit();
    return g_config.enabled && g_config.hideFromDirectInput && g_hid.HasProfileFor(vid, pid);
}

}  // namespace
}  // namespace uinput

using namespace uinput;

extern "C" {

DWORD WINAPI Proxy_XInputGetCapabilities(DWORD userIndex, DWORD flags, XCapabilities* caps) {
    EnsureInit();
    if (!caps || userIndex >= kMaxSlots) return ERROR_BAD_ARGUMENTS;
    (void)flags;  // queried without XINPUT_FLAG_GAMEPAD; the reported subtype is decided here

    XCapabilities real = {};
    DWORD result = g_ready ? g_real.getCapabilities(userIndex, 0, &real) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS) {
        g_hid.ReleaseSlot(userIndex);
        const BYTE realSubType = real.SubType;
        real.SubType = ResolveSubType(userIndex, realSubType);
        NoteSlot(userIndex, true, false, realSubType, real.SubType);
        *caps = real;
        return ERROR_SUCCESS;
    }
    if (g_config.enabled && g_hid.Query(userIndex, nullptr)) {
        FillVirtualCapabilities(userIndex, caps);
        NoteSlot(userIndex, true, true, kSubGuitarAlternate, caps->SubType);
        return ERROR_SUCCESS;
    }
    NoteSlot(userIndex, false, false, 0, 0);
    return result;
}

DWORD WINAPI Proxy_XInputGetState(DWORD userIndex, XState* state) {
    EnsureInit();
    if (!state || userIndex >= kMaxSlots) return ERROR_BAD_ARGUMENTS;
    DWORD result = g_ready ? g_real.getState(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS) return result;
    if (g_config.enabled && g_hid.Query(userIndex, state)) return ERROR_SUCCESS;
    return result;
}

DWORD WINAPI Proxy_XInputGetStateEx(DWORD userIndex, XState* state) {
    EnsureInit();
    if (!state || userIndex >= kMaxSlots) return ERROR_BAD_ARGUMENTS;
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;
    if (g_ready) result = g_real.getStateEx ? g_real.getStateEx(userIndex, state) : g_real.getState(userIndex, state);
    if (result == ERROR_SUCCESS) return result;
    if (g_config.enabled && g_hid.Query(userIndex, state)) return ERROR_SUCCESS;
    return result;
}

DWORD WINAPI Proxy_XInputSetState(DWORD userIndex, XVibration* vibration) {
    EnsureInit();
    if (!g_ready || !g_real.setState) return ERROR_DEVICE_NOT_CONNECTED;
    DWORD result = g_real.setState(userIndex, vibration);
    // Virtual guitars have no motors; accept the call so the game doesn't treat them as unplugged.
    if (result != ERROR_SUCCESS && userIndex < kMaxSlots && g_hid.Query(userIndex, nullptr)) return ERROR_SUCCESS;
    return result;
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
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // Only an import-table patch here (no library loading under the loader lock). GH3 may
        // create DirectInput before its first XInput call, so this must happen at load time.
        g_dinputHooked = InstallDirectInputHook(&ShouldHideFromDirectInput, &EnsureInit);
    }
    return TRUE;
}
