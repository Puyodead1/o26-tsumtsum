@echo off
rem Builds libacioEx.dll, o26tag.exe and o26tag_gui.exe (x64) with MSVC. Run from any prompt: in a Visual Studio
rem developer prompt (or CI with msvc-dev-cmd) it uses that, otherwise it finds the newest Visual Studio with vswhere.
if defined VSCMD_VER goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio with the C++ tools & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo no Visual Studio with the C++ x64 tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:build
cd /d "%~dp0"
cl /nologo /O2 /LD /MT libacioex_emu.c /Fe:libacioEx.dll /link /DLL user32.lib gdi32.lib advapi32.lib winscard.lib || exit /b 1
cl /nologo /O2 /MT o26tag.c /Fe:o26tag.exe /link winscard.lib || exit /b 1
cl /nologo /O2 /MT o26tag_gui.c /Fe:o26tag_gui.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib gdi32.lib comctl32.lib winscard.lib || exit /b 1
del /q libacioex_emu.obj o26tag.obj o26tag_gui.obj libacioEx.exp libacioEx.lib 2>nul
