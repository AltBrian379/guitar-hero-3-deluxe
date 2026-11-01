// XInput 1.3 ABI types, declared locally so we never collide with the SDK's
// <Xinput.h> declarations (which differ between SDK versions and target 1.4).
#pragma once
#include <windows.h>

namespace uinput {

struct XGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct XState {
    DWORD    dwPacketNumber;
    XGamepad Gamepad;
};

struct XVibration {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
};

struct XCapabilities {
    BYTE       Type;
    BYTE       SubType;
    WORD       Flags;
    XGamepad   Gamepad;
    XVibration Vibration;
};

struct XBatteryInformation {
    BYTE BatteryType;
    BYTE BatteryLevel;
};

struct XKeystroke {
    WORD  VirtualKey;
    WCHAR Unicode;
    WORD  Flags;
    BYTE  UserIndex;
    BYTE  HidCode;
};

constexpr DWORD kMaxSlots = 4;
constexpr DWORD kFlagGamepad = 0x00000001;  // XINPUT_FLAG_GAMEPAD
constexpr BYTE  kDevTypeGamepad = 0x01;     // XINPUT_DEVTYPE_GAMEPAD

// XINPUT_DEVSUBTYPE_*
enum SubType : BYTE {
    kSubGamepad         = 0x01,
    kSubWheel           = 0x02,
    kSubArcadeStick     = 0x03,
    kSubFlightStick     = 0x04,
    kSubDancePad        = 0x05,
    kSubGuitar          = 0x06,  // Rock Band style guitars
    kSubGuitarAlternate = 0x07,  // Guitar Hero guitars - the ONLY subtype GH3 PC treats as a guitar
    kSubDrumKit         = 0x08,
    kSubGuitarBass      = 0x0B,
    kSubArcadePad       = 0x13,
};

// XINPUT_GAMEPAD_* button bits
enum Button : WORD {
    kDpadUp    = 0x0001,
    kDpadDown  = 0x0002,
    kDpadLeft  = 0x0004,
    kDpadRight = 0x0008,
    kStart     = 0x0010,
    kBack      = 0x0020,
    kLThumb    = 0x0040,
    kRThumb    = 0x0080,
    kLShoulder = 0x0100,
    kRShoulder = 0x0200,
    kA         = 0x1000,
    kB         = 0x2000,
    kX         = 0x4000,
    kY         = 0x8000,
};

}  // namespace uinput
