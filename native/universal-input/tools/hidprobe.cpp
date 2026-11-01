// hidprobe - lists HID game controllers and records their input, to build guitar mappings.
//
// usage:
//   hidprobe.exe                      list joysticks/gamepads with their HID descriptor caps
//   hidprobe.exe VID:PID [seconds]    live capture: decoded button/value changes, then a
//                                     per-usage and per-raw-byte min/max summary

#include <windows.h>
// clang-format off
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

namespace {

struct Device {
    std::wstring path;
    USHORT vid = 0, pid = 0, version = 0;
    std::wstring product;
    HIDP_CAPS caps = {};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    std::vector<HIDP_BUTTON_CAPS> buttonCaps;
    std::vector<HIDP_VALUE_CAPS> valueCaps;
};

std::vector<Device> Enumerate() {
    std::vector<Device> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA iface = {sizeof(iface)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        std::vector<BYTE> buf(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr)) continue;

        Device d;
        d.path = detail->DevicePath;
        HANDLE h = CreateFileW(d.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attr = {sizeof(attr)};
        if (HidD_GetAttributes(h, &attr)) {
            d.vid = attr.VendorID;
            d.pid = attr.ProductID;
            d.version = attr.VersionNumber;
        }
        wchar_t product[128] = {};
        if (HidD_GetProductString(h, product, sizeof(product))) d.product = product;
        if (HidD_GetPreparsedData(h, &d.preparsed) && HidP_GetCaps(d.preparsed, &d.caps) == HIDP_STATUS_SUCCESS) {
            USHORT n = d.caps.NumberInputButtonCaps;
            d.buttonCaps.resize(n);
            if (n) HidP_GetButtonCaps(HidP_Input, d.buttonCaps.data(), &n, d.preparsed);
            d.buttonCaps.resize(n);
            n = d.caps.NumberInputValueCaps;
            d.valueCaps.resize(n);
            if (n) HidP_GetValueCaps(HidP_Input, d.valueCaps.data(), &n, d.preparsed);
            d.valueCaps.resize(n);
        }
        CloseHandle(h);
        out.push_back(std::move(d));
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

bool IsGameController(const Device& d) {
    return d.caps.UsagePage == 0x01 && (d.caps.Usage == 0x04 || d.caps.Usage == 0x05);
}

void PrintDevice(const Device& d) {
    const bool xinput = d.path.find(L"IG_") != std::wstring::npos || d.path.find(L"ig_") != std::wstring::npos;
    wprintf(L"%04X:%04X v%04X \"%s\"%s\n  %s\n", d.vid, d.pid, d.version, d.product.c_str(),
            xinput ? L"  [XInput device's HID shadow]" : L"", d.path.c_str());
    printf("  top usage %02X:%02X  input report %u bytes\n", d.caps.UsagePage, d.caps.Usage, d.caps.InputReportByteLength);
    for (const auto& b : d.buttonCaps) {
        if (b.IsRange)
            printf("  buttons: page %02X usages %u-%u (report %u)\n", b.UsagePage, b.Range.UsageMin, b.Range.UsageMax, b.ReportID);
        else
            printf("  button : page %02X usage %u (report %u)\n", b.UsagePage, b.NotRange.Usage, b.ReportID);
    }
    for (const auto& v : d.valueCaps) {
        USAGE u = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
        USAGE u2 = v.IsRange ? v.Range.UsageMax : u;
        char usage[16];
        if (v.IsRange)
            snprintf(usage, sizeof(usage), "%02X-%02X", u, u2);
        else
            snprintf(usage, sizeof(usage), "%02X", u);
        printf("  value  : page %02X usage %s  bits %u x%u  logical %ld..%ld  (report %u)\n", v.UsagePage, usage,
               v.BitSize, v.ReportCount, v.LogicalMin, v.LogicalMax, v.ReportID);
    }
}

struct Range {
    long lo = LONG_MAX, hi = LONG_MIN;
    void Add(long v) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
};

int Capture(const Device& d, double seconds) {
    HANDLE h = CreateFileW(d.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        printf("cannot open for reading (error %lu)\n", GetLastError());
        return 1;
    }
    const ULONG len = d.caps.InputReportByteLength;
    std::vector<char> report(len), last(len, 0);
    std::vector<Range> rawRange(len);
    std::map<std::string, Range> valueRange;
    std::string lastButtons;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const ULONGLONG start = GetTickCount64(), end = start + static_cast<ULONGLONG>(seconds * 1000);
    unsigned reports = 0;
    printf("capturing %.0f s...\n", seconds);

    while (GetTickCount64() < end) {
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        if (!ReadFile(h, report.data(), len, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;
        if (WaitForSingleObject(ov.hEvent, 100) != WAIT_OBJECT_0) {
            CancelIo(h);
            GetOverlappedResult(h, &ov, &got, TRUE);
            continue;
        }
        if (!GetOverlappedResult(h, &ov, &got, FALSE) || got == 0) continue;
        ++reports;
        for (ULONG i = 0; i < len; ++i) rawRange[i].Add(static_cast<unsigned char>(report[i]));
        const double t = (GetTickCount64() - start) / 1000.0;

        // Decoded buttons.
        std::string buttons;
        std::vector<USHORT> pagesDone;
        for (const auto& bc : d.buttonCaps) {
            bool done = false;
            for (USHORT p : pagesDone) done |= p == bc.UsagePage;
            if (done) continue;  // one button cap per button, but HidP_GetUsages reads the whole page
            pagesDone.push_back(bc.UsagePage);
            USAGE usages[128];
            ULONG n = 128;
            if (HidP_GetUsages(HidP_Input, bc.UsagePage, 0, usages, &n, d.preparsed, report.data(), len) ==
                HIDP_STATUS_SUCCESS) {
                for (ULONG k = 0; k < n; ++k) {
                    char tmp[24];
                    snprintf(tmp, sizeof(tmp), "%02X:%u ", bc.UsagePage, usages[k]);
                    buttons += tmp;
                }
            }
        }
        if (buttons != lastButtons) {
            printf("  t=%6.2f buttons [%s]\n", t, buttons.c_str());
            lastButtons = buttons;
        }
        // Decoded values (log big moves only, to keep noise down).
        for (const auto& vc : d.valueCaps) {
            USAGE lo = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            USAGE hi = vc.IsRange ? vc.Range.UsageMax : lo;
            for (USAGE u = lo; u <= hi; ++u) {
                ULONG value = 0;
                if (HidP_GetUsageValue(HidP_Input, vc.UsagePage, 0, u, &value, d.preparsed, report.data(), len) !=
                    HIDP_STATUS_SUCCESS)
                    continue;
                char key[24];
                snprintf(key, sizeof(key), "%02X:%02X", vc.UsagePage, u);
                Range& r = valueRange[key];
                const long span = vc.LogicalMax > vc.LogicalMin ? vc.LogicalMax - vc.LogicalMin : 255;
                const bool first = r.lo == LONG_MAX;
                const long prevHi = r.hi, prevLo = r.lo;
                r.Add(static_cast<long>(value));
                if (first || r.hi != prevHi || r.lo != prevLo) {
                    if (!first && (r.hi - r.lo) * 8 >= span) printf("  t=%6.2f value %s = %lu (range now %ld..%ld)\n", t, key, value, r.lo, r.hi);
                }
            }
        }
        last = report;
    }
    CancelIo(h);
    CloseHandle(ov.hEvent);
    CloseHandle(h);

    printf("\n%u reports. value ranges:\n", reports);
    for (const auto& [k, r] : valueRange) printf("  %s: %ld..%ld\n", k.c_str(), r.lo, r.hi);
    printf("raw bytes that changed (index: min..max):\n");
    for (ULONG i = 0; i < len; ++i)
        if (rawRange[i].lo != rawRange[i].hi) printf("  [%2lu] %3ld..%3ld\n", i, rawRange[i].lo, rawRange[i].hi);
    printf("last report:");
    for (ULONG i = 0; i < len; ++i) printf(" %02X", static_cast<unsigned char>(last[i]));
    printf("\n");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    auto devices = Enumerate();
    if (argc < 2) {
        for (const auto& d : devices)
            if (IsGameController(d)) PrintDevice(d);
        return 0;
    }
    unsigned vid = 0, pid = 0;
    if (swscanf_s(argv[1], L"%x:%x", &vid, &pid) != 2) {
        fprintf(stderr, "expected VID:PID in hex\n");
        return 1;
    }
    const double seconds = argc > 2 ? _wtof(argv[2]) : 20.0;
    for (const auto& d : devices) {
        if (d.vid == vid && d.pid == pid && IsGameController(d)) {
            PrintDevice(d);
            return Capture(d, seconds);
        }
    }
    fprintf(stderr, "no game controller %04X:%04X found\n", vid, pid);
    return 1;
}
