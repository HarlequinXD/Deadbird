@echo off
rem Deadbird for Windows with Visual Studio: run from a "x64 Native Tools Command Prompt".
rem /MT links the C runtime in, so no Visual C++ redistributable is needed.
rem Built for size: /O1 /Gy + /OPT:REF,ICF drop unused code; everything else is generated at start-up.
rc /nologo /fo deadbird.res deadbird.rc || exit /b 1
cl /nologo /std:c11 /O1 /Os /Gy /GF /GS- /W3 /MT /D_CRT_SECURE_NO_WARNINGS /I..\..\engine /I..\..\game /I..\..\gen /I..\..\assets ^
   win_main.c ..\..\engine\*.c ..\..\game\*.c ..\..\gen\*.c deadbird.res ^
   /Fe:Deadbird.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF user32.lib gdi32.lib winmm.lib xinput9_1_0.lib || exit /b 1
for %%F in (Deadbird.exe) do echo Deadbird.exe: %%~zF bytes
