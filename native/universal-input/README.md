# GH3 Universal Input (PC)

A drop-in `xinput1_3.dll` for Guitar Hero III PC that lets the game recognise guitar controllers it
otherwise rejects, without changing how regular gamepads behave.

## Why it is needed

GH3 PC only treats an XInput controller as a guitar when it reports
`XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE` (7). Rock Band-style guitars (6), bass guitars (0x0B), and every
non-XInput controller are handled as plain gamepads. Through DirectInput, a device is only a guitar if its
product name matches one of four hard-coded names.

## What it does (milestone 1)

- Forwards every XInput call to the system `xinput1_3.dll`.
- In each controller slot, it reports guitars of any XInput subtype as subtype 7, so GH3 treats them as guitars.
- It leaves regular gamepads untouched.
- Each slot has an override in `gh3_universal_input.ini`: `Auto` / `Guitar` / `Gamepad`.
- It writes `gh3_universal_input.log` next to `GH3.exe`, recording connections and detected types.

Planned next: a HID backend for non-XInput guitars (PS3/Wii/HID-mode Santroller, Raspberry Pi builds) with a
setup wizard, hiding those devices from GH3's DirectInput so they don't appear twice, and an in-game toggle.

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

`uitest.exe [path\to\xinput1_3.dll] [seconds]` loads a DLL the same way GH3 does (by ordinal, with
`XINPUT_FLAG_GAMEPAD`). It prints how GH3 would classify each slot and can show live input.
