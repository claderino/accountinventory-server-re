REM ============================================================================
REM build.bat - build the reconstructed bsConsole_sv64.dll with MSVC (x64).
REM   build\bsConsole_sv64.dll  +  build\bsConsole_sv64.lib
REM Usage:  build.bat            build
REM         build.bat /clean     remove build output first
REM ============================================================================
@echo off
setlocal EnableDelayedExpansion
if /I "%~1"=="/clean" if exist "%~dp0build" rmdir /s /q "%~dp0build"

set "MSVC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
if not exist "%MSVC%\bin\Hostx64\x64\cl.exe" for /d %%m in ("C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*") do if not defined MSVC2 set "MSVC2=%%~m"
if defined MSVC2 set "MSVC=%MSVC2%"
set "WININC=C:\Program Files (x86)\Windows Kits\10\Include"
for /f "delims=" %%v in ('dir /b /ad "%WININC%\10.0.*" /o-n') do if not defined SDKVER set "SDKVER=%%v"
set "UCRT=%WININC%\%SDKVER%\ucrt"
set "UM=%WININC%\%SDKVER%\um"
set "SHARED=%WININC%\%SDKVER%\shared"
set "WINLIB=C:\Program Files (x86)\Windows Kits\10\Lib\%SDKVER%"
set "SRC=%~dp0src"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

set "CPPFILES="
for /r "%SRC%" %%f in (*.cpp) do set "CPPFILES=!CPPFILES! "%%f""

echo [build] compiling bsConsole_sv64.dll...
"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /O2 /W3 /utf-8 /MT /Zc:wchar_t /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0601 ^
  /I"%MSVC%\include" /I"%UCRT%" /I"%UM%" /I"%SHARED%" /I"%SRC%" /I"%~dp0..\AchievementDaemon\src\BloodLib" ^
  /Fo"%OUT%\\" %CPPFILES% ^
  /Fe:"%OUT%\bsConsole_sv64.dll" /LD ^
  /link /IMPLIB:"%OUT%\bsConsole_sv64.lib" /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%WINLIB%\ucrt\x64" /LIBPATH:"%WINLIB%\um\x64" ^
  user32.lib gdi32.lib comdlg32.lib advapi32.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD OK: %OUT%\bsConsole_sv64.dll
