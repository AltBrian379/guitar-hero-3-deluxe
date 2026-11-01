#include "hid_device.h"

// clang-format off
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
// clang-format on

#include <cwctype>

#include "log.h"

namespace uinput {
namespace {

bool ContainsIgnoreCase(const std::wstring& haystack, const wchar_t* needle) {
    std::wstring h = haystack, n = needle;
    for (auto& c : h) c = static_cast<wchar_t>(towupper(c));
    for (auto& c : n) c = static_cast<wchar_t>(towupper(c));
    return h.find(n) != std::wstring::npos;
}

}  // namespace

std::vector<HidDeviceInfo> EnumerateHidGameControllers() {
    std::vector<HidDeviceInfo> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;

    SP_DEVICE_INTERFACE_DATA iface = {};
    iface.cbSize = sizeof(iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        if (size == 0) continue;
        std::vector<BYTE> buf(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr)) continue;

        HidDeviceInfo info;
        info.path = detail->DevicePath;
        HANDLE h = CreateFileW(info.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        bool isGameController = false;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(h, &preparsed)) {
            HIDP_CAPS caps = {};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
                isGameController = caps.UsagePage == kPageGenericDesktop && (caps.Usage == 0x04 || caps.Usage == 0x05);
            HidD_FreePreparsedData(preparsed);
        }
        if (isGameController) {
            HIDD_ATTRIBUTES attr = {};
            attr.Size = sizeof(attr);
            if (HidD_GetAttributes(h, &attr)) {
                info.vid = attr.VendorID;
                info.pid = attr.ProductID;
            }
            wchar_t product[128] = {};
            if (HidD_GetProductString(h, product, sizeof(product))) info.product = product;
            info.xinputShadow = ContainsIgnoreCase(info.path, L"IG_");
            out.push_back(std::move(info));
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::unique_ptr<HidDevice> HidDevice::Open(const HidDeviceInfo& info) {
    std::unique_ptr<HidDevice> dev(new HidDevice());
    dev->info_ = info;
    dev->handle_ = CreateFileW(info.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (dev->handle_ == INVALID_HANDLE_VALUE) {
        Log("HID: cannot open %ls (error %lu)", info.path.c_str(), GetLastError());
        return nullptr;
    }
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps = {};
    if (!HidD_GetPreparsedData(dev->handle_, &preparsed) || HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) {
        if (preparsed) HidD_FreePreparsedData(preparsed);
        return nullptr;  // destructor closes the handle
    }
    dev->preparsed_ = preparsed;
    dev->reportLength_ = caps.InputReportByteLength;

    USHORT n = caps.NumberInputButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> buttonCaps(n);
    if (n && HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &n, preparsed) == HIDP_STATUS_SUCCESS) {
        for (USHORT i = 0; i < n; ++i) {
            bool seen = false;
            for (USHORT p : dev->buttonPages_) seen |= p == buttonCaps[i].UsagePage;
            if (!seen) dev->buttonPages_.push_back(buttonCaps[i].UsagePage);
        }
    }
    n = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> valueCaps(n);
    if (n && HidP_GetValueCaps(HidP_Input, valueCaps.data(), &n, preparsed) == HIDP_STATUS_SUCCESS) {
        for (USHORT i = 0; i < n; ++i) {
            const HIDP_VALUE_CAPS& vc = valueCaps[i];
            if (vc.ReportCount > 1) continue;  // value arrays are not used by any guitar we know of
            const USAGE lo = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            const USAGE hi = vc.IsRange ? vc.Range.UsageMax : lo;
            for (USAGE u = lo; u >= lo && u <= hi; ++u) {
                HidValueInfo vi;
                vi.logicalMin = vc.LogicalMin;
                vi.logicalMax = vc.LogicalMax;
                // Some descriptors leave logical max unset/unsigned-wrapped; fall back to the bit range.
                if (vi.logicalMax <= vi.logicalMin && vc.BitSize < 32) {
                    vi.logicalMin = 0;
                    vi.logicalMax = static_cast<long>((1UL << vc.BitSize) - 1);
                }
                vi.isHat = vc.UsagePage == kPageGenericDesktop && u == kUsageHatSwitch;
                dev->valueInfo_[UsageKey(vc.UsagePage, u)] = vi;
                dev->valueSources_.push_back({vc.UsagePage, u, vc.BitSize, vi.logicalMin < 0});
                if (u == hi) break;
            }
        }
    }

    dev->stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dev->alive_ = true;
    dev->snapshot_.connected = true;
    dev->thread_ = CreateThread(nullptr, 0, &HidDevice::ThreadMain, dev.get(), 0, nullptr);
    if (!dev->thread_) {
        dev->alive_ = false;
        return nullptr;
    }
    return dev;
}

HidDevice::~HidDevice() {
    if (thread_) {
        SetEvent(stopEvent_);
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
    }
    if (stopEvent_) CloseHandle(stopEvent_);
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    if (preparsed_) HidD_FreePreparsedData(static_cast<PHIDP_PREPARSED_DATA>(preparsed_));
}

HidSnapshot HidDevice::Snapshot() const {
    AcquireSRWLockShared(&lock_);
    HidSnapshot s = snapshot_;
    ReleaseSRWLockShared(&lock_);
    return s;
}

DWORD WINAPI HidDevice::ThreadMain(LPVOID self) {
    static_cast<HidDevice*>(self)->ReadLoop();
    return 0;
}

void HidDevice::ReadLoop() {
    std::vector<char> report(reportLength_ ? reportLength_ : 64);
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE waits[2] = {ov.hEvent, stopEvent_};
    for (;;) {
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        if (!ReadFile(handle_, report.data(), static_cast<DWORD>(report.size()), nullptr, &ov) &&
            GetLastError() != ERROR_IO_PENDING) {
            Log("HID: read failed on %04X:%04X (error %lu)", info_.vid, info_.pid, GetLastError());
            break;
        }
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
            CancelIoEx(handle_, &ov);
            GetOverlappedResult(handle_, &ov, &got, TRUE);
            break;
        }
        if (!GetOverlappedResult(handle_, &ov, &got, FALSE)) {
            Log("HID: %04X:%04X disconnected (error %lu)", info_.vid, info_.pid, GetLastError());
            break;
        }
        if (got > 0) Parse(report.data(), got);
    }
    CloseHandle(ov.hEvent);
    AcquireSRWLockExclusive(&lock_);
    snapshot_.connected = false;
    ReleaseSRWLockExclusive(&lock_);
    alive_ = false;
}

void HidDevice::Parse(char* report, ULONG length) {
    auto preparsed = static_cast<PHIDP_PREPARSED_DATA>(preparsed_);
    AcquireSRWLockExclusive(&lock_);
    HidSnapshot next = snapshot_;
    ReleaseSRWLockExclusive(&lock_);

    for (USHORT page : buttonPages_) {
        USAGE usages[128];
        ULONG count = 128;
        // INCOMPATIBLE_REPORT_ID just means this report does not carry that page; keep the old state.
        if (HidP_GetUsages(HidP_Input, page, 0, usages, &count, preparsed, report, length) != HIDP_STATUS_SUCCESS)
            continue;
        for (auto it = next.buttons.begin(); it != next.buttons.end();)
            it = UsagePage(*it) == page ? next.buttons.erase(it) : std::next(it);
        for (ULONG i = 0; i < count; ++i) next.buttons.insert(UsageKey(page, usages[i]));
    }
    for (const ValueSource& src : valueSources_) {
        ULONG raw = 0;
        if (HidP_GetUsageValue(HidP_Input, src.page, 0, src.usage, &raw, preparsed, report, length) !=
            HIDP_STATUS_SUCCESS)
            continue;
        long value = static_cast<long>(raw);
        if (src.isSigned && src.bitSize > 0 && src.bitSize < 32 && (raw & (1UL << (src.bitSize - 1))))
            value = static_cast<long>(raw | ~((1UL << src.bitSize) - 1));
        next.values[UsageKey(src.page, src.usage)] = value;
    }

    AcquireSRWLockExclusive(&lock_);
    if (next.buttons != snapshot_.buttons || next.values != snapshot_.values) {
        next.sequence = snapshot_.sequence + 1;
        snapshot_ = std::move(next);
    }
    ReleaseSRWLockExclusive(&lock_);
}

int HatDirection(long value, const HidValueInfo& info) {
    const long positions = info.logicalMax - info.logicalMin + 1;
    const long d = value - info.logicalMin;
    if (d < 0 || d >= positions) return -1;
    if (positions == 8) return static_cast<int>(d);
    if (positions == 4) return static_cast<int>(d * 2);
    return -1;
}

}  // namespace uinput
