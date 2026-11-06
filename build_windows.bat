@echo off
rem Native Windows build with MSVC (Visual Studio 2019/2022 Build Tools).
rem Run from any command prompt; vcvars64 is located automatically via vswhere.
setlocal
cd /d "%~dp0"
if defined VCINSTALLDIR goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
:build
if not exist build_msvc mkdir build_msvc
cl /nologo /std:c++17 /O2 /Oi /GL /EHsc /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /W3 /MP src\*.cpp /Fo:build_msvc\ /Fe:engine.exe /link /LTCG ws2_32.lib
endlocal
