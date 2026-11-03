@echo off
rem Boot Disney Tsum Tsum (O26) with spice2x. Run from the game folder (the one with modules\, prop\, data\, arkdata\, dev\).
rem Start the e-amusement server first: asphyxia-core-x64.exe with this repo's server\plugins\o26@asphyxia installed.
rem   -runas user        no UAC elevation (spice elevates by default)
rem   -exec / -modules   arko26.dll is the AVS game DLL; spice doesn't know O26, so point it there
rem   -url               Asphyxia CORE; AVS rejects 127.0.0.1, so use localhost
rem                      (no server: use "-ea -eamaint 0" instead; boots to attract, but card login fails)
rem   -z d3d9.dll        preload d3d9 so spice's D3D9 hook applies (tmac.dll loads after graphics init)
rem   -overlaydisable    spice's overlay crashes on this game
rem   -s                 show the mouse pointer (the game hides it)
rem   -w -windowsize     windowed at half size, so the portrait 1080x1920 screen fits a 1080p monitor
rem                      (drop all three for fullscreen on a portrait 1080x1920 monitor)
rem Keys: F1 TEST, F2 SERVICE, F9 coin, F11 e-amusement card (card0.txt), F12 mascot figure (figure0.txt), mouse = touch.
cd /d "%~dp0"
if not exist dev\nvram mkdir dev\nvram
if not exist dev\raw\log mkdir dev\raw\log
start "" spice64.exe -runas user -modules modules -exec arko26.dll -url http://localhost:8083 -z d3d9.dll -overlaydisable -s -w -windowsize 540,960 -windowpos 0,0 %*
