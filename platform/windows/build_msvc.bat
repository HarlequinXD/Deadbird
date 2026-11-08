@echo off
rem Deadbird for Windows with Visual Studio: run from a "x64 Native Tools Command Prompt".
rem /MT links the C runtime in, so no Visual C++ redistributable is needed.
rc /nologo /fo deadbird.res deadbird.rc || exit /b 1
cl /nologo /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /I..\..\engine /I..\..\game /I..\..\assets win_main.c ..\..\engine\*.c ..\..\game\*.c deadbird.res ^
   /Fe:Deadbird.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib gdi32.lib winmm.lib xinput9_1_0.lib || exit /b 1
