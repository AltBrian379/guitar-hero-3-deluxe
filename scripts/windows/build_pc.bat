@echo off

cd "%~dp0..\.."
python dependencies\python\gen_version.py _qb/dx/dx_version.q
python dependencies\python\configure_build.py pc

if %errorlevel% neq 0 (pause /b %errorlevel% && exit /b %errorlevel%)

call "%~dp0build_universal_input.bat"
if %errorlevel% neq 0 (pause /b %errorlevel% && exit /b %errorlevel%)

start "" "%~dp0..\..\out\pc"