@echo off
rem Builds GH3 Universal Input (xinput1_3.dll + guitar setup wizard) and adds it to out\pc.
rem Needs CMake and Visual Studio's x86 C++ tools; skipped with a warning when CMake is missing.

cd /d "%~dp0..\.."

where cmake >nul 2>nul
if %errorlevel% neq 0 (
	echo WARNING: cmake not found - GH3 Universal Input was not built.
	exit /b 0
)

cmake -S native\universal-input -B native\universal-input\build -A Win32
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build native\universal-input\build --config Release --target xinput1_3 gh3-guitar-setup
if %errorlevel% neq 0 exit /b %errorlevel%

if not exist out\pc mkdir out\pc
copy /y native\universal-input\build\Release\xinput1_3.dll out\pc\ >nul
copy /y native\universal-input\build\Release\gh3-guitar-setup.exe out\pc\ >nul
rem Shipped under a different name so updating never overwrites a player's saved mappings.
copy /y native\universal-input\gh3_universal_input.ini out\pc\gh3_universal_input.example.ini >nul
copy /y native\universal-input\README.md out\pc\GH3_Universal_Input_README.md >nul
echo GH3 Universal Input added to out\pc
