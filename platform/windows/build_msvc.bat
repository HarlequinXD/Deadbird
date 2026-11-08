@echo off
rem Deadbird for Windows with Visual Studio: run from a "x64 Native Tools Command Prompt".
cl /nologo /O2 /W3 /I..\..\engine /I..\..\game /I..\..\assets win_main.c ..\..\engine\*.c ..\..\game\*.c ^
   /Fe:Deadbird.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib
