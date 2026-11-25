@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /O2 /W3 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0601 /Fe:"%~dp0probe.exe" "%~dp0probe.cpp"
