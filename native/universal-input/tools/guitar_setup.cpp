// gh3-guitar-setup - interactive mapping wizard for non-XInput guitars.
//
// Run it from the Guitar Hero III folder. It asks you to press each fret, strum, push the
// whammy and tilt the guitar, detects which HID input changed, and saves a
// [Device.VVVV:PPPP] section to gh3_universal_input.ini next to this program
// (or to the path given as the first argument).

#include <windows.h>
#include <conio.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/hid_device.h"
#include "../src/mapping.h"

using namespace uinput;

namespace {

constexpr int kKeyEnter = 13;
constexpr int kKeyEsc = 27;

struct Baseline {
    HidSnapshot snap;
    std::map<unsigned, long> noise;  // max deviation seen while idle
};

std::wstring DefaultIniPath() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/")) + L"\\gh3_universal_input.ini";
}

// Returns -1 (no key), kKeyEnter, kKeyEsc or another key code.
int PollKey() {
    if (!_kbhit()) return -1;
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        _getch();
        return -1;
    }
    return c;
}

Baseline CaptureBaseline(HidDevice& dev) {
    Baseline b;
    b.snap = dev.Snapshot();
    const ULONGLONG end = GetTickCount64() + 1500;
    while (GetTickCount64() < end) {
        HidSnapshot s = dev.Snapshot();
        for (const auto& [key, v] : s.values) {
            long d = std::labs(v - b.snap.values[key]);
            if (d > b.noise[key]) b.noise[key] = d;
        }
        Sleep(10);
    }
    return b;
}

bool IsHat(const HidDevice& dev, unsigned key) {
    auto it = dev.Values().find(key);
    return it != dev.Values().end() && it->second.isHat;
}

// Looks for a digital input that differs from the baseline. Buttons and hats win over axes;
// axes must move more than 60% of their range (so guitar tilt/whammy wobble is ignored).
bool DetectDigital(const HidDevice& dev, const Baseline& base, const HidSnapshot& s, DigitalBinding& out) {
    for (unsigned key : s.buttons) {
        if (!base.snap.buttons.count(key)) {
            out = {DigitalBinding::Button, key, 0};
            return true;
        }
    }
    for (const auto& [key, v] : s.values) {
        if (!IsHat(dev, key)) continue;
        auto bi = base.snap.values.find(key);
        if (bi != base.snap.values.end() && bi->second == v) continue;
        const int d = HatDirection(v, dev.Values().at(key));
        static const DigitalBinding::Kind kDir[8] = {DigitalBinding::HatUp,   DigitalBinding::HatUp,   DigitalBinding::HatRight,
                                                     DigitalBinding::HatDown, DigitalBinding::HatDown, DigitalBinding::HatDown,
                                                     DigitalBinding::HatLeft, DigitalBinding::HatUp};
        if (d < 0 || d % 2) continue;  // wait for a clean cardinal direction
        out = {kDir[d], key, 0};
        return true;
    }
    for (const auto& [key, v] : s.values) {
        if (IsHat(dev, key)) continue;
        auto bi = base.snap.values.find(key);
        if (bi == base.snap.values.end()) continue;
        const long span = dev.Values().at(key).Span();
        if (std::labs(v - bi->second) * 10 < span * 6) continue;
        out = {v > bi->second ? DigitalBinding::AxisAbove : DigitalBinding::AxisBelow, key, (v + bi->second) / 2};
        return true;
    }
    return false;
}

bool SameSource(const DigitalBinding& a, const DigitalBinding& b) {
    return a.kind != DigitalBinding::None && a.kind == b.kind && a.key == b.key;
}

void WaitForRelease(HidDevice& dev, const DigitalBinding& b) {
    while (IsDigitalActive(b, dev.Snapshot(), dev.Values())) Sleep(10);
    Sleep(150);  // debounce
}

// Finds the axis that moved furthest from rest (>25% of range), excluding `exclude`, then
// tracks its extreme for a moment while the player holds it.
bool DetectAnalog(HidDevice& dev, const Baseline& base, unsigned exclude, AnalogBinding& out, bool allowButton) {
    for (;;) {
        int key = PollKey();
        if (key == kKeyEnter) return false;
        if (key == kKeyEsc) exit(1);
        HidSnapshot s = dev.Snapshot();
        if (allowButton) {
            for (unsigned k : s.buttons) {
                if (!base.snap.buttons.count(k)) {
                    out = {AnalogBinding::Button, k, 0, 1};
                    return true;
                }
            }
        }
        unsigned bestKey = 0;
        double best = 0.25;
        for (const auto& [k, v] : s.values) {
            if (k == exclude || IsHat(dev, k)) continue;
            auto bi = base.snap.values.find(k);
            if (bi == base.snap.values.end()) continue;
            const double moved = std::labs(v - bi->second) / static_cast<double>(dev.Values().at(k).Span());
            if (moved > best) {
                best = moved;
                bestKey = k;
            }
        }
        if (bestKey) {
            const long rest = base.snap.values.at(bestKey);
            long extreme = s.values.at(bestKey);
            const ULONGLONG end = GetTickCount64() + 1200;
            while (GetTickCount64() < end) {
                long v = dev.Snapshot().values[bestKey];
                if (std::labs(v - rest) > std::labs(extreme - rest)) extreme = v;
                Sleep(10);
            }
            out = {AnalogBinding::Axis, bestKey, rest, extreme};
            return true;
        }
        Sleep(10);
    }
}

void WaitAnalogRelease(HidDevice& dev, const AnalogBinding& b) {
    if (b.kind == AnalogBinding::None) return;
    while (AnalogValue(b, dev.Snapshot()) > 0.2f) Sleep(10);
    Sleep(150);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring iniPath = argc > 1 ? argv[1] : DefaultIniPath();
    printf("GH3 Universal Input - guitar setup\n==================================\n\n");

    std::vector<HidDeviceInfo> devices;
    for (const auto& d : EnumerateHidGameControllers())
        if (!d.xinputShadow) devices.push_back(d);
    if (devices.empty()) {
        printf("No non-XInput controllers found.\n"
               "Plug in your guitar and run this again. (Xbox 360 / XInput guitars need no setup.)\n");
        printf("\nPress any key to exit.");
        _getch();
        return 1;
    }
    printf("Controllers found:\n");
    for (size_t i = 0; i < devices.size(); ++i)
        printf("  %zu) %04X:%04X  %ls\n", i + 1, devices[i].vid, devices[i].pid, devices[i].product.c_str());
    size_t pick = 0;
    if (devices.size() > 1) {
        printf("Which one is your guitar? [1-%zu] ", devices.size());
        for (;;) {
            int c = _getch();
            if (c == kKeyEsc) return 1;
            if (c >= '1' && c < '1' + static_cast<int>(devices.size())) {
                pick = static_cast<size_t>(c - '1');
                break;
            }
        }
        printf("%zu\n", pick + 1);
    }
    auto dev = HidDevice::Open(devices[pick]);
    if (!dev) {
        printf("Could not open the controller (is another program using it exclusively?).\n");
        _getch();
        return 1;
    }

    Profile profile;
    profile.vid = devices[pick].vid;
    profile.pid = devices[pick].pid;
    profile.name = devices[pick].product.empty() ? L"Guitar" : devices[pick].product;

    printf("\nHold the guitar normally and don't touch anything...\n");
    Sleep(500);
    Baseline base = CaptureBaseline(*dev);
    printf("OK.\n\nPress each control when asked. Enter = skip, Esc = quit.\n\n");

    static const char* kPrompts[kNumDigital] = {"GREEN fret", "RED fret",  "YELLOW fret", "BLUE fret", "ORANGE fret",
                                                "STRUM UP",   "STRUM DOWN", "START",       "SELECT / Star Power button"};
    for (int c = 0; c < kNumDigital; ++c) {
        printf("  Press %-28s ", kPrompts[c]);
        fflush(stdout);
        for (;;) {
            int key = PollKey();
            if (key == kKeyEsc) return 1;
            if (key == kKeyEnter) {
                printf("skipped\n");
                break;
            }
            DigitalBinding b;
            if (DetectDigital(*dev, base, dev->Snapshot(), b)) {
                bool dup = false;
                for (int prev = 0; prev < c; ++prev) dup |= SameSource(profile.digital[prev], b);
                if (dup) {
                    printf("\n    That input is already used - release it and press %s ", kPrompts[c]);
                    WaitForRelease(*dev, b);
                    continue;
                }
                profile.digital[c] = b;
                printf("%s\n", FormatDigital(b).c_str());
                WaitForRelease(*dev, b);
                break;
            }
            Sleep(10);
        }
    }

    printf("\n  Push the WHAMMY all the way and hold it...   ");
    fflush(stdout);
    if (DetectAnalog(*dev, base, 0, profile.whammy, false)) {
        printf("%s\n", FormatAnalog(profile.whammy).c_str());
        printf("  Release the whammy.\n");
        WaitAnalogRelease(*dev, profile.whammy);
    } else {
        printf("skipped\n");
    }

    printf("\n  TILT the guitar up (star power pose) and hold it...   ");
    fflush(stdout);
    const unsigned whammyKey = profile.whammy.kind == AnalogBinding::Axis ? profile.whammy.key : 0;
    if (DetectAnalog(*dev, base, whammyKey, profile.tilt, true)) {
        // Trigger star power a bit before the full pose the player demonstrated.
        if (profile.tilt.kind == AnalogBinding::Axis)
            profile.tilt.full = profile.tilt.rest + (profile.tilt.full - profile.tilt.rest) * 7 / 10;
        printf("%s\n", FormatAnalog(profile.tilt).c_str());
        printf("  Lower the guitar again.\n");
        WaitAnalogRelease(*dev, profile.tilt);
    } else {
        printf("skipped\n");
    }

    printf("\nSave this mapping to\n  %ls\n? [Y/n] ", iniPath.c_str());
    for (;;) {
        int c = _getch();
        if (c == 'n' || c == 'N' || c == kKeyEsc) {
            printf("not saved.\n");
            return 1;
        }
        if (c == 'y' || c == 'Y' || c == kKeyEnter) break;
    }
    if (!SaveProfile(iniPath, profile)) {
        printf("\nFailed to write the file (error %lu).\n", GetLastError());
        _getch();
        return 1;
    }
    printf("\nSaved [Device.%04X:%04X]. Start Guitar Hero III and your guitar will appear as a guitar.\n", profile.vid,
           profile.pid);
    printf("\nPress any key to exit.");
    _getch();
    return 0;
}
