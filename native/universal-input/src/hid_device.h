// Generic HID game-controller access: enumeration, and a background reader that keeps an
// up-to-date, descriptor-decoded snapshot (pressed button usages + value usages).
#pragma once
#include <windows.h>

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace uinput {

// Identifies a HID usage: (usage page << 16) | usage.
inline unsigned UsageKey(unsigned page, unsigned usage) { return (page << 16) | (usage & 0xFFFF); }
inline unsigned UsagePage(unsigned key) { return key >> 16; }
inline unsigned UsageId(unsigned key) { return key & 0xFFFF; }
constexpr unsigned kPageGenericDesktop = 0x01;
constexpr unsigned kPageButton = 0x09;
constexpr unsigned kUsageHatSwitch = 0x39;

struct HidValueInfo {
    long logicalMin = 0;
    long logicalMax = 0;
    bool isHat = false;
    long Span() const { return logicalMax > logicalMin ? logicalMax - logicalMin : 1; }
};

struct HidSnapshot {
    bool connected = false;
    DWORD sequence = 0;               // bumps whenever any decoded input changes
    std::set<unsigned> buttons;       // pressed button usage keys
    std::map<unsigned, long> values;  // value usage key -> logical value
};

struct HidDeviceInfo {
    std::wstring path;
    USHORT vid = 0;
    USHORT pid = 0;
    std::wstring product;
    // XInput controllers also expose a HID interface (path contains "IG_"); those are
    // handled through XInput and must not be mapped twice.
    bool xinputShadow = false;
};

// All present HID devices whose top-level collection is a joystick or gamepad.
std::vector<HidDeviceInfo> EnumerateHidGameControllers();

class HidDevice {
public:
    static std::unique_ptr<HidDevice> Open(const HidDeviceInfo& info);
    ~HidDevice();
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    const HidDeviceInfo& Info() const { return info_; }
    const std::map<unsigned, HidValueInfo>& Values() const { return valueInfo_; }
    HidSnapshot Snapshot() const;
    bool Alive() const { return alive_.load(); }

private:
    HidDevice() = default;
    static DWORD WINAPI ThreadMain(LPVOID self);
    void ReadLoop();
    void Parse(char* report, ULONG length);

    HidDeviceInfo info_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    void* preparsed_ = nullptr;  // PHIDP_PREPARSED_DATA
    ULONG reportLength_ = 0;
    std::vector<USHORT> buttonPages_;
    struct ValueSource {
        USHORT page;
        USHORT usage;
        USHORT bitSize;
        bool isSigned;
    };
    std::vector<ValueSource> valueSources_;
    std::map<unsigned, HidValueInfo> valueInfo_;

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    HidSnapshot snapshot_;
    std::atomic<bool> alive_{false};
};

// Hat switch value -> direction 0..7 (0 = up, clockwise), or -1 when centred / out of range.
int HatDirection(long value, const HidValueInfo& info);

}  // namespace uinput
