// uitest - exercises an xinput1_3.dll exactly the way GH3.exe does (imports by ordinal,
// GetCapabilities with XINPUT_FLAG_GAMEPAD), and prints what the game would see.
//
// usage: uitest.exe [path\to\xinput1_3.dll] [seconds_of_live_output]

#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "../src/xinput_types.h"

using namespace uinput;

using GetStateFn = DWORD(WINAPI*)(DWORD, XState*);
using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XCapabilities*);

// Mirrors GH3.exe 0x522a7c: SubType -> internal device type.
static const char* Gh3DeviceKind(BYTE subType) {
    switch (subType) {
        case kSubGuitarAlternate: return "GUITAR (type 8)";
        case kSubWheel: return "wheel (type 0x300)";
        case kSubArcadeStick:
        case kSubFlightStick: return "stick (type 5)";
        default: return "gamepad (type 7)";
    }
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* dllPath = argc > 1 ? argv[1] : L"xinput1_3.dll";
    const double seconds = argc > 2 ? _wtof(argv[2]) : 0.0;

    HMODULE dll = LoadLibraryW(dllPath);
    if (!dll) {
        fwprintf(stderr, L"cannot load %s (error %lu)\n", dllPath, GetLastError());
        return 1;
    }
    auto getState = reinterpret_cast<GetStateFn>(GetProcAddress(dll, MAKEINTRESOURCEA(2)));
    auto getCaps = reinterpret_cast<GetCapabilitiesFn>(GetProcAddress(dll, MAKEINTRESOURCEA(4)));
    auto enable = GetProcAddress(dll, MAKEINTRESOURCEA(5));
    wchar_t loaded[MAX_PATH];
    GetModuleFileNameW(dll, loaded, MAX_PATH);
    wprintf(L"loaded %s\n  ordinal 2=%p 4=%p 5=%p\n", loaded, getState, getCaps, enable);
    if (!getState || !getCaps || !enable) {
        fprintf(stderr, "missing ordinal export(s)\n");
        return 1;
    }

    for (DWORD i = 0; i < kMaxSlots; ++i) {
        XCapabilities caps = {};
        DWORD r = getCaps(i, kFlagGamepad, &caps);
        if (r != ERROR_SUCCESS) {
            printf("slot %lu: not connected (%lu)\n", i + 1, r);
            continue;
        }
        XState st = {};
        getState(i, &st);
        printf("slot %lu: Type=%u SubType=%u -> GH3 sees %s | buttons=%04x RX=%d RY=%d\n", i + 1, caps.Type,
               caps.SubType, Gh3DeviceKind(caps.SubType), st.Gamepad.wButtons, st.Gamepad.sThumbRX,
               st.Gamepad.sThumbRY);
    }

    if (seconds > 0) {
        printf("live output for %.0f s (changes only)\n", seconds);
        XState last[kMaxSlots] = {};
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000);
        while (GetTickCount64() < end) {
            for (DWORD i = 0; i < kMaxSlots; ++i) {
                XState st = {};
                if (getState(i, &st) != ERROR_SUCCESS) continue;
                const XGamepad& g = st.Gamepad;
                const XGamepad& p = last[i].Gamepad;
                if (g.wButtons != p.wButtons || abs(g.sThumbRX - p.sThumbRX) > 512 || abs(g.sThumbRY - p.sThumbRY) > 512) {
                    printf("  slot %lu buttons=%04x RX=%6d RY=%6d\n", i + 1, g.wButtons, g.sThumbRX, g.sThumbRY);
                    last[i] = st;
                }
            }
            Sleep(5);
        }
    }
    return 0;
}
