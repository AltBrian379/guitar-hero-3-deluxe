#include "game_bridge.h"

#include <cstring>

#include "log.h"

namespace uinput {
namespace {

// --- GH3 PC v1.3 (GH3.exe, image base 0x400000, no ASLR) -------------------------------
// Global symbol table: CSymbolTableEntry* table[0x8000], indexed by (QbKey & 0x7FFF).
//   entry+0x04 = name QbKey, entry+0x0C = value (int/float), entry+0x10 = next in bucket
// Seen at 0x42E089: mov edx, [0xC03AA8] ; mov ecx, eax ; and ecx, 0x7FFF ; ...
constexpr uintptr_t kSymbolTablePtr = 0xC03AA8;
constexpr uintptr_t kSymbolLookupSite = 0x42E089;
constexpr BYTE kSymbolLookupBytes[] = {0x8B, 0x15, 0xA8, 0x3A, 0xC0, 0x00, 0x8B, 0xC8};
constexpr unsigned kSymbolBucketMask = 0x7FFF;

// Controller N (0-3) -> device: [[0xB4631C] + 0x20 + N*4]; its input backend object at +0x1C
// holds +0x10 = XInput user index and +0xEC = backend (0 = XInput).
// Seen at 0x40ED69: mov ecx, [0xB4631C] ; mov eax, [ecx + eax*4 + 0x20] ; ret
constexpr uintptr_t kDeviceArrayPtr = 0xB4631C;
constexpr uintptr_t kDeviceLookupSite = 0x40ED69;
constexpr BYTE kDeviceLookupBytes[] = {0x8B, 0x0D, 0x1C, 0x63, 0xB4, 0x00, 0x8B, 0x44, 0x81, 0x20, 0xC3};

// QbKeys of dx_input_mode_device_0..3 (CRC-32 of the lowercase name, no final xor).
unsigned QbKey(const char* s) {
    unsigned crc = 0xFFFFFFFF;
    for (; *s; ++s) {
        crc ^= static_cast<unsigned char>(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return crc;
}

bool CodeMatches(uintptr_t address, const BYTE* expected, size_t length) {
    __try {
        return memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// All raw memory access is SEH-guarded: a wrong guess must never crash the game.
bool ReadGlobalInt(unsigned key, int* out) {
    __try {
        auto table = *reinterpret_cast<uintptr_t* const*>(kSymbolTablePtr);
        if (!table) return false;
        uintptr_t entry = table[key & kSymbolBucketMask];
        for (int hops = 0; entry && hops < 256; ++hops) {
            if (*reinterpret_cast<const unsigned*>(entry + 0x04) == key) {
                *out = *reinterpret_cast<const int*>(entry + 0x0C);
                return true;
            }
            entry = *reinterpret_cast<const uintptr_t*>(entry + 0x10);
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Returns the XInput slot GH3's controller N reads from, or -1.
int SlotOfController(int controller) {
    __try {
        uintptr_t array = *reinterpret_cast<const uintptr_t*>(kDeviceArrayPtr);
        if (!array) return -1;
        uintptr_t device = *reinterpret_cast<const uintptr_t*>(array + 0x20 + controller * 4);
        if (!device) return -1;
        uintptr_t backend = *reinterpret_cast<const uintptr_t*>(device + 0x1C);
        if (!backend) return -1;
        if (*reinterpret_cast<const int*>(backend + 0xEC) != 0) return -1;  // not an XInput device
        int slot = *reinterpret_cast<const int*>(backend + 0x10);
        return slot >= 0 && slot < static_cast<int>(kMaxSlots) ? slot : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

const char* ModeName(GameInputMode m) {
    switch (m) {
        case GameInputMode::Guitar: return "Guitar";
        case GameInputMode::Gamepad: return "Gamepad";
        default: return "Auto";
    }
}

}  // namespace

void GameBridge::Init() {
    available_ = CodeMatches(kSymbolLookupSite, kSymbolLookupBytes, sizeof(kSymbolLookupBytes)) &&
                 CodeMatches(kDeviceLookupSite, kDeviceLookupBytes, sizeof(kDeviceLookupBytes));
    Log("in-game input mode bridge: %s", available_ ? "enabled (GH3 v1.3 detected)"
                                                    : "disabled (unrecognised executable; INI modes only)");
}

void GameBridge::Refresh() {
    if (!available_) return;
    const ULONGLONG now = GetTickCount64();
    if (now - lastRefresh_ < 250) return;
    lastRefresh_ = now;

    static const unsigned kKeys[4] = {QbKey("dx_input_mode_device_0"), QbKey("dx_input_mode_device_1"),
                                      QbKey("dx_input_mode_device_2"), QbKey("dx_input_mode_device_3")};
    GameInputMode modes[kMaxSlots] = {};
    for (int controller = 0; controller < 4; ++controller) {
        int value = 0;
        if (!ReadGlobalInt(kKeys[controller], &value) || value < 1 || value > 2) continue;
        // Controller N normally reads XInput slot N; trust the game's own mapping when it has one.
        int slot = SlotOfController(controller);
        if (slot < 0) slot = controller;
        modes[slot] = static_cast<GameInputMode>(value);
    }
    for (DWORD s = 0; s < kMaxSlots; ++s) {
        slotMode_[s] = modes[s];
        if (loggedMode_[s] != modes[s]) {
            Log("slot %lu: in-game input mode -> %s", s + 1, ModeName(modes[s]));
            loggedMode_[s] = modes[s];
        }
    }
}

}  // namespace uinput
