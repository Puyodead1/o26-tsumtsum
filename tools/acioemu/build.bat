@echo off
rem Builds libacioEx.dll, o26tag.exe and o26tag_gui.exe (x64) with MSVC. Run from any prompt.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /LD /MT libacioex_emu.c /Fe:libacioEx.dll /link /DLL user32.lib gdi32.lib advapi32.lib winscard.lib || exit /b 1
cl /nologo /O2 /MT o26tag.c /Fe:o26tag.exe /link winscard.lib || exit /b 1
cl /nologo /O2 /MT o26tag_gui.c /Fe:o26tag_gui.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib gdi32.lib comctl32.lib winscard.lib || exit /b 1
del /q libacioex_emu.obj o26tag.obj o26tag_gui.obj libacioEx.exp libacioEx.lib 2>nul
