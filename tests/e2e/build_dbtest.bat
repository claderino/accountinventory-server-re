@echo off
set "MSVC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "WININC=C:\Program Files (x86)\Windows Kits\10\Include"
for /f "delims=" %%v in ('dir /b /ad "%WININC%\10.0.*" /o-n') do if not defined SDKVER set "SDKVER=%%v"
set "UCRT=%WININC%\%SDKVER%\ucrt"
set "UM=%WININC%\%SDKVER%\um"
set "SHARED=%WININC%\%SDKVER%\shared"
set "WINLIB=C:\Program Files (x86)\Windows Kits\10\Lib\%SDKVER%"
set "ROOT=C:\serverside_re\accountinventory"
"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /Od /Zi /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DNOMINMAX /I"%MSVC%\include" /I"%UCRT%" /I"%UM%" /I"%SHARED%" /I"%ROOT%\AccountInventoryDaemon\src\BloodLib" /I"%ROOT%\AccountInventoryDaemon\src\AccountInventory" "%~dp0dbtest.cpp" "%ROOT%\AccountInventoryDaemon\src\AccountInventory\Db.cpp" "%ROOT%\AccountInventoryDaemon\src\AccountInventory\AccountInventoryDB.cpp" /Fe:"%~dp0dbtest.exe" /link /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%WINLIB%\ucrt\x64" /LIBPATH:"%WINLIB%\um\x64" odbc32.lib odbccp32.lib
