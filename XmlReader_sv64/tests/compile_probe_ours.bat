@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /O2 /W3 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0601 /I"C:\serverside_re\ranking\RankingDaemon\src\BloodLib" /Fo"C:\serverside_re\ranking\XmlReader_sv64\tests\\" /Fe:"%~dp0probe_ours.exe" "%~dp0probe_ours.cpp" "C:\serverside_re\ranking\RankingDaemon\src\BloodLib\XmlReaderLib.cpp" "%~dp0log_stub.cpp"
