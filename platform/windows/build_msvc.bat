@echo off
rem Deadbird for Windows with Visual Studio: run from a "x64 Native Tools Command Prompt".
rem Built for size: no C runtime library (tinycrt.c supplies the little the game needs, so there's
rem no Visual C++ redistributable either), /O1 /Gy + /OPT:REF,ICF drop unused code, and the level,
rem sounds and textures are generated at start-up.
rc /nologo /fo deadbird.res deadbird.rc || exit /b 1
cl /nologo /std:c11 /O1 /Os /Oi /Gy /GF /GS- /W3 /D_CRT_SECURE_NO_WARNINGS /I..\..\engine /I..\..\game /I..\..\gen /I..\..\assets ^
   ..\..\engine\platform\windows\win_main.c ..\..\engine\platform\windows\tinycrt.c ..\..\engine\*.c ..\..\game\*.c ..\..\gen\*.c deadbird.res ^
   /Fe:Deadbird.exe /link /SUBSYSTEM:WINDOWS /ENTRY:start /NODEFAULTLIB /MANIFEST:NO /OPT:REF /OPT:ICF ^
   kernel32.lib user32.lib gdi32.lib winmm.lib xinput9_1_0.lib || exit /b 1
for %%F in (Deadbird.exe) do echo Deadbird.exe: %%~zF bytes
