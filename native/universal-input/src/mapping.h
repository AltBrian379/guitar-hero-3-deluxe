// Guitar mapping profiles: which HID input drives which guitar control, stored in
// gh3_universal_input.ini as [Device.VVVV:PPPP] sections.
//
// Binding syntax (usage numbers are hex; a bare usage is on the Generic Desktop page,
// "PP:UU" names another page):
//   button <n>              HID button n (page 9), or button PP:UU
//   hat <up|down|left|right> [usage]    hat switch direction (default usage 39)
//   axis <usage> > <value>  digital: pressed while the axis is above value
//   axis <usage> < <value>  digital: pressed while the axis is below value
//   axis <usage> <rest> <full>   analog (whammy/tilt): rest -> 0.0, full -> 1.0
//   none
#pragma once
#include <map>
#include <string>
#include <vector>

#include "hid_device.h"
#include "xinput_types.h"

namespace uinput {

enum DigitalControl {
    kCtlGreen, kCtlRed, kCtlYellow, kCtlBlue, kCtlOrange, kCtlStrumUp, kCtlStrumDown, kCtlStart, kCtlSelect, kNumDigital
};
const char* DigitalControlKey(int control);  // INI key, e.g. "StrumUp"

struct DigitalBinding {
    enum Kind { None, Button, HatUp, HatDown, HatLeft, HatRight, AxisAbove, AxisBelow } kind = None;
    unsigned key = 0;     // usage key
    long threshold = 0;   // AxisAbove / AxisBelow
};

struct AnalogBinding {
    enum Kind { None, Axis, Button } kind = None;
    unsigned key = 0;
    long rest = 0;   // Axis: raw value meaning 0.0
    long full = 0;   // Axis: raw value meaning 1.0
};

struct Profile {
    USHORT vid = 0;
    USHORT pid = 0;
    std::wstring name;
    DigitalBinding digital[kNumDigital];
    AnalogBinding whammy;
    AnalogBinding tilt;
};

std::string FormatDigital(const DigitalBinding& b);
std::string FormatAnalog(const AnalogBinding& b);
bool ParseDigital(const std::string& text, DigitalBinding& out);
bool ParseAnalog(const std::string& text, AnalogBinding& out);

std::vector<Profile> LoadProfiles(const std::wstring& iniPath);
bool SaveProfile(const std::wstring& iniPath, const Profile& profile);

bool IsDigitalActive(const DigitalBinding& b, const HidSnapshot& s, const std::map<unsigned, HidValueInfo>& info);
float AnalogValue(const AnalogBinding& b, const HidSnapshot& s);  // 0..1

// Builds the Xbox 360 Guitar Hero guitar layout GH3 expects:
// A/B/Y/X/LB = green/red/yellow/blue/orange, D-pad up/down = strum, Start, Back = select,
// right stick X = whammy (-32768 at rest), right stick Y = tilt.
XGamepad MapToGuitar(const Profile& p, const HidSnapshot& s, const std::map<unsigned, HidValueInfo>& info);

}  // namespace uinput
