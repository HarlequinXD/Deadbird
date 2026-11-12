@echo off
rem Deadbird Studio for Windows with Visual Studio: run from a "x64 Native Tools Command Prompt".
rem The editor uses the C runtime (linked statically: no redistributable needed) and /O2, since its
rem full-resolution UI is drawn in software.
rc /nologo /fo studio.res studio.rc || exit /b 1
cl /nologo /std:c11 /O2 /MT /W3 /DWIN_HIRES /D_CRT_SECURE_NO_WARNINGS ^
   /I..\..\engine /I..\..\engine\fw /I..\..\engine\studio /I..\..\studio /I..\..\examples\fps /I..\..\game /I..\..\gen /I..\..\assets ^
   ..\..\engine\platform\windows\win_main.c ..\..\engine\*.c ..\..\engine\fw\*.c ..\..\engine\studio\*.c ..\..\studio\*.c ^
   ..\..\examples\fps\sector_world.c ..\..\game\level.c ..\..\game\edmap.c ..\..\game\editor.c ..\..\game\edui.c ..\..\game\ed3d.c ^
   ..\..\game\ui.c ..\..\game\assets.c ..\..\game\fx.c ..\..\game\hud.c ..\..\gen\*.c studio.res ^
   /Fe:DeadbirdStudio.exe /link /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup /MANIFEST:NO /OPT:REF /OPT:ICF ^
   kernel32.lib user32.lib gdi32.lib winmm.lib xinput9_1_0.lib || exit /b 1
for %%F in (DeadbirdStudio.exe) do echo DeadbirdStudio.exe: %%~zF bytes
