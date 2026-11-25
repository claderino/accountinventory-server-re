# Build bsConsole_sv64.dll and run the differential test against the original DLL.
# The test must run in the interactive foreground session (it creates real windows).
$ErrorActionPreference = 'Stop'
$proj = Split-Path -Parent $PSScriptRoot
$orig = 'C:\serverside_re\ranking\analysis\dll\orig\bsConsole_sv64.dll'
$mine = Join-Path $proj 'build\bsConsole_sv64.dll'
$testExe = Join-Path $proj 'build\difftest.exe'

Write-Host '[test] building DLL...'
& (Join-Path $proj 'build.bat')
if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED'; exit 1 }

$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
if (-not (Test-Path "$msvc\bin\Hostx64\x64\cl.exe")) {
    $found = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*' -Directory -ErrorAction SilentlyContinue |
             Select-Object -First 1 -ExpandProperty FullName
    if ($found) { $msvc = $found }
}
$wininc = 'C:\Program Files (x86)\Windows Kits\10\Include'
$sdkver = (Get-ChildItem "$wininc\10.0.*" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$ucrt = "$wininc\$sdkver\ucrt"
$um = "$wininc\$sdkver\um"
$shared = "$wininc\$sdkver\shared"
$winlib = "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkver"

if (-not (Test-Path $orig)) { Write-Host "FAIL: original DLL not found: $orig"; exit 2 }

Write-Host '[test] compiling difftest.exe...'
& "$msvc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /O2 /W3 /utf-8 /MT /Zc:wchar_t `
  /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0601 `
  "/I$msvc\include" "/I$ucrt" "/I$um" "/I$shared" `
  '/I"C:\serverside_re\ranking\RankingDaemon\src\BloodLib"' `
  /Fo"$($proj)\build\\" /Fe:"$testExe" `
  (Join-Path $PSScriptRoot 'difftest.cpp') `
  /link "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$winlib\ucrt\x64" "/LIBPATH:$winlib\um\x64" user32.lib gdi32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { Write-Host 'TEST COMPILE FAILED'; exit 1 }

Write-Host '[test] running differential test...'
& $testExe $orig $mine
$code = $LASTEXITCODE
exit $code
