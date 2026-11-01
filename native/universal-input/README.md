# GH3 Universal Input (PC)

A drop-in `xinput1_3.dll` for Guitar Hero III PC that lets the game recognise guitar controllers it
otherwise rejects, without changing how regular gamepads behave. It ships with the GH3DX PC build.

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
- **In-game "P1 / P2 Input Mode"** (Deluxe Settings → Modifiers): `Auto` / `Guitar` / `Gamepad` for the
  controller each player is using.
  - The setting is saved with your other options.
  - The DLL reads it straight from the game's script globals. This only works on the GH3 PC v1.3 executable;
    on any other executable the DLL detects the mismatch, ignores the in-game setting and uses the INI.
- **Per-slot defaults** in `gh3_universal_input.ini`: `Auto` / `Guitar` / `Gamepad`. The in-game setting
  wins when it isn't `Auto`.
- **Log:** `gh3_universal_input.log` next to `GH3.exe`, recording connections, mappings, mode changes and
  hidden DirectInput devices.

## Installing

1. Extract the GH3DX PC build into the Guitar Hero III folder as usual. It includes `xinput1_3.dll`,
   `gh3-guitar-setup.exe` and `gh3_universal_input.example.ini`.
2. Remove any x360ce files (`xinput1_3.dll`, `x360ce.ini`) from earlier setups. GH3DX's DLL replaces them.
3. If your guitar sends clean whammy and tilt signals (for example a Santroller), disable the GH3+ `WhammyFix`
   and `TiltFix` plugins by moving them to `PLUGINS\inactive`. WhammyFix deliberately jitters the whammy value,
   and TiltFix's stricter tilt rule stops star power activating on these guitars.

`gh3_universal_input.ini` is optional, and the release never contains it. Updating GH3DX therefore never
overwrites your mappings. To change the slot defaults, copy `gh3_universal_input.example.ini` to
`gh3_universal_input.ini` and edit it.

## Setting up a non-XInput guitar

1. Run `gh3-guitar-setup.exe` from the Guitar Hero III folder.
2. Follow the prompts: frets, strum, Start/Select, whammy, tilt. Enter skips a control.
3. It saves a `[Device.VVVV:PPPP]` section to `gh3_universal_input.ini`. The format is documented in the
   example INI.

XInput guitars, such as a Santroller in XInput mode or an Xbox 360 guitar, need no setup.

## Built-in profiles (not done)

Every non-XInput guitar currently needs the setup wizard once. Built-in profiles for common hardware
(PS3 Guitar Hero / Rock Band guitars, Wii guitars through popular adapters, Santroller's HID and PS3 modes)
would make those work out of the box. They are not included because they need testing on that hardware.
To contribute one:

1. Run the wizard.
2. Check the result in-game.
3. Share the `[Device.VVVV:PPPP]` section it wrote.

## Building

You need Visual Studio 2022 with the x86 C++ tools, and CMake 3.20 or newer. The DLL **must** be 32-bit.
`scripts/windows/build_pc.bat` builds it automatically, via `build_universal_input.bat`, and copies it into
`out/pc`. To build it on its own:

```
cmake -S native/universal-input -B native/universal-input/build -A Win32
cmake --build native/universal-input/build --config Release
```

The Linux build scripts do not build the DLL, because it needs MSVC.

## How it works (GH3 PC v1.3 internals)

| What | Where |
|---|---|
| XInput imports | `XINPUT1_3.dll` by ordinal: #2 GetState, #4 GetCapabilities, #5 Enable |
| Guitar detection | `0x522a64`: `XInputGetCapabilities(i, XINPUT_FLAG_GAMEPAD)` then a switch on SubType; only 7 → device type 8 (guitar) |
| Analog input | `0x52451c`: right stick X → whammy byte, right stick Y → tilt byte |
| DirectInput guitars | Product name must be `Guitar Hero3 for PlayStation (R) 3`, `Guitar Hero for PC/MAC`, `PSX/USB Pad` or `Tigergame Guitar` |
| Script globals | Hash table at `[0xC03AA8]`, 0x8000 buckets by QbKey; entry `+4` key, `+0xC` value, `+0x10` next |
| Controller N → device | `[[0xB4631C] + 0x20 + N*4]`; backend at `+0x1C` (`+0x10` XInput index, `+0xEC` backend kind) |

## Developer tools

- `uitest.exe [path\to\xinput1_3.dll] [seconds]` loads a DLL the same way GH3 does: by ordinal, with
  `XINPUT_FLAG_GAMEPAD`, and with DirectInput reached through its import table.
  - It lists what DirectInput enumerates and how GH3 would classify each slot.
  - It can also show live input.
- `hidprobe.exe [VID:PID [seconds]]` lists HID game controllers with their descriptor and records raw input.
