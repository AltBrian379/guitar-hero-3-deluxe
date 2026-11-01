# GH3 Universal Input (PC)

A drop-in `xinput1_3.dll` for Guitar Hero III PC that lets the game recognise guitar controllers it
otherwise rejects, without changing how regular gamepads behave.

## Why it is needed

GH3 PC only treats an XInput controller as a guitar when it reports
`XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE` (7). Rock Band-style guitars (6), bass guitars (0x0B), and every
non-XInput controller are handled as plain gamepads. Through DirectInput, a device is only a guitar if its
product name matches one of four hard-coded names.

## What it does

- **XInput controllers:**
  - Forwarded to the system `xinput1_3.dll`.
  - Guitars of any XInput subtype are reported as subtype 7, so GH3 treats them as guitars.
  - Regular gamepads are untouched.
- **Non-XInput guitars** (HID/PS3-mode Santroller, Wii and PS3 adapters, Raspberry Pi builds):
  - `gh3-guitar-setup.exe` records a mapping.
  - The DLL then reads the device directly and presents it to GH3 in the first free slot, as an Xbox 360 GH
    guitar: A/B/Y/X/LB frets, D-pad strum, Start/Back, whammy on right stick X, tilt on right stick Y.
  - The device is hidden from GH3's DirectInput enumeration, so it doesn't also show up as a gamepad.
- **Per-slot override** in `gh3_universal_input.ini`: `Auto` / `Guitar` / `Gamepad`.
- **Log:** `gh3_universal_input.log` next to `GH3.exe`, recording connections, mappings and hidden
  DirectInput devices.

Planned: built-in profiles for common guitars, and an in-game P1/P2 gamepad-mode toggle.

## Setting up a non-XInput guitar

1. Copy `gh3-guitar-setup.exe` into the Guitar Hero III folder and run it.
2. Follow the prompts: frets, strum, Start/Select, whammy, tilt. Enter skips a control.
3. It saves a `[Device.VVVV:PPPP]` section to `gh3_universal_input.ini`. The format is documented in that file.

## Building

You need Visual Studio 2022 with the x86 C++ tools, and CMake 3.20 or newer. The DLL **must** be 32-bit.

```
cmake -S native/universal-input -B native/universal-input/build -A Win32
cmake --build native/universal-input/build --config Release
```

The build writes `build/Release/xinput1_3.dll`, `gh3_universal_input.ini` and `uitest.exe`.

## Installing

Copy `xinput1_3.dll` and `gh3_universal_input.ini` next to `GH3.exe`. Remove any x360ce `xinput1_3.dll`
first. To uninstall, delete both files.

If your guitar already sends clean whammy and tilt signals (for example a Santroller in XInput mode), disable the GH3+
`WhammyFix` and `TiltFix` plugins. WhammyFix deliberately jitters the whammy value, and TiltFix's stricter
tilt rule prevents star power activation on these guitars.

## Testing without the game

- `uitest.exe [path\to\xinput1_3.dll] [seconds]` loads a DLL the same way GH3 does: by ordinal, with
  `XINPUT_FLAG_GAMEPAD`, and with DirectInput reached through its import table.
  - It lists what DirectInput enumerates and how GH3 would classify each slot.
  - It can also show live input.
- `hidprobe.exe [VID:PID [seconds]]` lists HID game controllers with their descriptor and records raw input.
