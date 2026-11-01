#include "mapping.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace uinput {
namespace {

const char* kDigitalKeys[kNumDigital] = {"Green", "Red", "Yellow", "Blue", "Orange", "StrumUp", "StrumDown", "Start", "Select"};

std::string FormatUsage(unsigned key, unsigned defaultPage) {
    char buf[16];
    if (UsagePage(key) == defaultPage)
        snprintf(buf, sizeof(buf), "%X", UsageId(key));
    else
        snprintf(buf, sizeof(buf), "%X:%X", UsagePage(key), UsageId(key));
    return buf;
}

bool ParseUsage(const std::string& token, unsigned defaultPage, unsigned& key) {
    char* end = nullptr;
    const size_t colon = token.find(':');
    if (colon == std::string::npos) {
        unsigned long usage = strtoul(token.c_str(), &end, 16);
        if (end == token.c_str() || *end) return false;
        key = UsageKey(defaultPage, usage);
        return true;
    }
    unsigned long page = strtoul(token.substr(0, colon).c_str(), &end, 16);
    unsigned long usage = strtoul(token.c_str() + colon + 1, &end, 16);
    if (*end) return false;
    key = UsageKey(page, usage);
    return true;
}

// Button numbers are written in decimal ("button 12"), matching how HID tools and
// Windows' game controller panel number them.
std::string FormatButton(unsigned key) {
    if (UsagePage(key) == kPageButton) return std::to_string(UsageId(key));
    return FormatUsage(key, kPageGenericDesktop);
}

bool ParseButton(const std::string& token, unsigned& key) {
    if (token.find(':') != std::string::npos) return ParseUsage(token, kPageButton, key);
    char* end = nullptr;
    unsigned long n = strtoul(token.c_str(), &end, 10);
    if (end == token.c_str() || *end) return false;
    key = UsageKey(kPageButton, n);
    return true;
}

std::vector<std::string> Split(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    for (std::string t; in >> t;) {
        for (auto& c : t) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        out.push_back(t);
    }
    return out;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string ReadIni(const std::wstring& ini, const std::wstring& section, const char* key) {
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(section.c_str(), Widen(key).c_str(), L"", buf, 256, ini.c_str());
    // Strip trailing ';' comments.
    std::wstring v = buf;
    size_t semi = v.find(L';');
    if (semi != std::wstring::npos) v.resize(semi);
    return Narrow(v);
}

}  // namespace

const char* DigitalControlKey(int control) { return control >= 0 && control < kNumDigital ? kDigitalKeys[control] : "?"; }

std::string FormatDigital(const DigitalBinding& b) {
    const char* dirs[] = {"up", "down", "left", "right"};
    char buf[64];
    switch (b.kind) {
        case DigitalBinding::None: return "none";
        case DigitalBinding::Button: return "button " + FormatButton(b.key);
        case DigitalBinding::HatUp:
        case DigitalBinding::HatDown:
        case DigitalBinding::HatLeft:
        case DigitalBinding::HatRight: {
            std::string s = std::string("hat ") + dirs[b.kind - DigitalBinding::HatUp];
            if (b.key != UsageKey(kPageGenericDesktop, kUsageHatSwitch)) s += " " + FormatUsage(b.key, kPageGenericDesktop);
            return s;
        }
        case DigitalBinding::AxisAbove:
        case DigitalBinding::AxisBelow:
            snprintf(buf, sizeof(buf), "axis %s %c %ld", FormatUsage(b.key, kPageGenericDesktop).c_str(),
                     b.kind == DigitalBinding::AxisAbove ? '>' : '<', b.threshold);
            return buf;
    }
    return "none";
}

std::string FormatAnalog(const AnalogBinding& b) {
    char buf[64];
    switch (b.kind) {
        case AnalogBinding::None: return "none";
        case AnalogBinding::Button: return "button " + FormatButton(b.key);
        case AnalogBinding::Axis:
            snprintf(buf, sizeof(buf), "axis %s %ld %ld", FormatUsage(b.key, kPageGenericDesktop).c_str(), b.rest, b.full);
            return buf;
    }
    return "none";
}

bool ParseDigital(const std::string& text, DigitalBinding& out) {
    auto t = Split(text);
    out = {};
    if (t.empty() || t[0] == "none") return true;
    if (t[0] == "button" && t.size() == 2) {
        out.kind = DigitalBinding::Button;
        return ParseButton(t[1], out.key);
    }
    if (t[0] == "hat" && (t.size() == 2 || t.size() == 3)) {
        static const char* dirs[] = {"up", "down", "left", "right"};
        for (int i = 0; i < 4; ++i)
            if (t[1] == dirs[i]) out.kind = static_cast<DigitalBinding::Kind>(DigitalBinding::HatUp + i);
        if (out.kind == DigitalBinding::None) return false;
        out.key = UsageKey(kPageGenericDesktop, kUsageHatSwitch);
        return t.size() == 2 || ParseUsage(t[2], kPageGenericDesktop, out.key);
    }
    if (t[0] == "axis" && t.size() == 4 && (t[2] == ">" || t[2] == "<")) {
        out.kind = t[2] == ">" ? DigitalBinding::AxisAbove : DigitalBinding::AxisBelow;
        out.threshold = strtol(t[3].c_str(), nullptr, 10);
        return ParseUsage(t[1], kPageGenericDesktop, out.key);
    }
    return false;
}

bool ParseAnalog(const std::string& text, AnalogBinding& out) {
    auto t = Split(text);
    out = {};
    if (t.empty() || t[0] == "none") return true;
    if (t[0] == "button" && t.size() == 2) {
        out.kind = AnalogBinding::Button;
        return ParseButton(t[1], out.key);
    }
    if (t[0] == "axis" && t.size() == 4) {
        out.kind = AnalogBinding::Axis;
        out.rest = strtol(t[2].c_str(), nullptr, 10);
        out.full = strtol(t[3].c_str(), nullptr, 10);
        return ParseUsage(t[1], kPageGenericDesktop, out.key) && out.rest != out.full;
    }
    return false;
}

std::vector<Profile> LoadProfiles(const std::wstring& iniPath) {
    std::vector<Profile> out;
    std::vector<wchar_t> names(32768);
    DWORD n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), iniPath.c_str());
    for (const wchar_t* s = names.data(); s < names.data() + n && *s; s += wcslen(s) + 1) {
        unsigned vid = 0, pid = 0;
        if (_wcsnicmp(s, L"Device.", 7) != 0 || swscanf_s(s + 7, L"%x:%x", &vid, &pid) != 2) continue;
        const std::wstring section = s;
        Profile p;
        p.vid = static_cast<USHORT>(vid);
        p.pid = static_cast<USHORT>(pid);
        p.name = Widen(ReadIni(iniPath, section, "Name"));
        bool ok = true;
        for (int c = 0; c < kNumDigital; ++c) ok &= ParseDigital(ReadIni(iniPath, section, kDigitalKeys[c]), p.digital[c]);
        ok &= ParseAnalog(ReadIni(iniPath, section, "Whammy"), p.whammy);
        ok &= ParseAnalog(ReadIni(iniPath, section, "Tilt"), p.tilt);
        if (ok) out.push_back(p);
    }
    return out;
}

bool SaveProfile(const std::wstring& iniPath, const Profile& p) {
    wchar_t section[32];
    swprintf_s(section, L"Device.%04X:%04X", p.vid, p.pid);
    bool ok = WritePrivateProfileStringW(section, L"Name", p.name.c_str(), iniPath.c_str()) != 0;
    for (int c = 0; c < kNumDigital; ++c)
        ok &= WritePrivateProfileStringW(section, Widen(kDigitalKeys[c]).c_str(), Widen(FormatDigital(p.digital[c])).c_str(),
                                         iniPath.c_str()) != 0;
    ok &= WritePrivateProfileStringW(section, L"Whammy", Widen(FormatAnalog(p.whammy)).c_str(), iniPath.c_str()) != 0;
    ok &= WritePrivateProfileStringW(section, L"Tilt", Widen(FormatAnalog(p.tilt)).c_str(), iniPath.c_str()) != 0;
    return ok;
}

bool IsDigitalActive(const DigitalBinding& b, const HidSnapshot& s, const std::map<unsigned, HidValueInfo>& info) {
    switch (b.kind) {
        case DigitalBinding::None: return false;
        case DigitalBinding::Button: return s.buttons.count(b.key) != 0;
        case DigitalBinding::AxisAbove:
        case DigitalBinding::AxisBelow: {
            auto it = s.values.find(b.key);
            if (it == s.values.end()) return false;
            return b.kind == DigitalBinding::AxisAbove ? it->second > b.threshold : it->second < b.threshold;
        }
        default: {
            auto it = s.values.find(b.key);
            if (it == s.values.end()) return false;
            HidValueInfo hat = {0, 7, true};
            auto ii = info.find(b.key);
            if (ii != info.end()) hat = ii->second;
            const int d = HatDirection(it->second, hat);
            if (d < 0) return false;
            switch (b.kind) {
                case DigitalBinding::HatUp: return d == 7 || d == 0 || d == 1;
                case DigitalBinding::HatRight: return d >= 1 && d <= 3;
                case DigitalBinding::HatDown: return d >= 3 && d <= 5;
                case DigitalBinding::HatLeft: return d >= 5 && d <= 7;
                default: return false;
            }
        }
    }
}

float AnalogValue(const AnalogBinding& b, const HidSnapshot& s) {
    switch (b.kind) {
        case AnalogBinding::None: return 0.0f;
        case AnalogBinding::Button: return s.buttons.count(b.key) ? 1.0f : 0.0f;
        case AnalogBinding::Axis: {
            auto it = s.values.find(b.key);
            if (it == s.values.end() || b.full == b.rest) return 0.0f;
            float t = static_cast<float>(it->second - b.rest) / static_cast<float>(b.full - b.rest);
            return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        }
    }
    return 0.0f;
}

XGamepad MapToGuitar(const Profile& p, const HidSnapshot& s, const std::map<unsigned, HidValueInfo>& info) {
    static const WORD kButtonFor[kNumDigital] = {kA, kB, kY, kX, kLShoulder, kDpadUp, kDpadDown, kStart, kBack};
    XGamepad g = {};
    for (int c = 0; c < kNumDigital; ++c)
        if (IsDigitalActive(p.digital[c], s, info)) g.wButtons |= kButtonFor[c];
    // Whammy: -32768 at rest .. 32767 fully pressed (what GH 360 guitars send).
    g.sThumbRX = static_cast<SHORT>(-32768 + static_cast<long>(AnalogValue(p.whammy, s) * 65535.0f));
    // Tilt: a level 360 guitar rests around -16000; fully tilted is 32767.
    g.sThumbRY = static_cast<SHORT>(-16384 + static_cast<long>(AnalogValue(p.tilt, s) * 49151.0f));
    return g;
}

}  // namespace uinput
