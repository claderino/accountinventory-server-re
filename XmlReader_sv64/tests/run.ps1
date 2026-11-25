# Differential test driver: build the DLL, compile difftest.cpp against the frozen
# XmlReaderLib.cpp host loader, run it and report PASS/FAIL with exact counts.
param([switch]$SkipBuild)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tests = Join-Path $root 'tests'
$out = Join-Path $root 'build'
$blood = 'C:\serverside_re\ranking\RankingDaemon\src\BloodLib'

$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
if (-not (Test-Path (Join-Path $msvc 'bin\Hostx64\x64\cl.exe'))) {
    $found = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*' -Directory |
             Select-Object -First 1 -ExpandProperty FullName
    if ($found) { $msvc = $found }
}
$wininc = 'C:\Program Files (x86)\Windows Kits\10\Include'
$sdkver = (Get-ChildItem -Path $wininc -Directory -Filter '10.0.*' | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name)
$winlib = 'C:\Program Files (x86)\Windows Kits\10\Lib\' + $sdkver
$cl = Join-Path $msvc 'bin\Hostx64\x64\cl.exe'

if (-not $SkipBuild) {
    Write-Host '[run] building DLL...'
    & (Join-Path $root 'build.bat')
    if ($LASTEXITCODE -ne 0) { throw 'build.bat failed' }
}

Write-Host '[run] compiling difftest...'
$obj = Join-Path $out 'difftest.obj'
$exe = Join-Path $out 'difftest.exe'
$inc = @("/I`"$msvc\include`"", "/I`"$wininc\$sdkver\ucrt`"", "/I`"$wininc\$sdkver\um`"",
         "/I`"$wininc\$sdkver\shared`"", "/I`"$blood`"")
$flags = @('/nologo', '/EHsc', '/std:c++17', '/O2', '/W3', '/utf-8', '/MT', '/Zc:wchar_t',
           '/D_CRT_SECURE_NO_WARNINGS', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/D_WIN32_WINNT=0x0601')
$src = @((Join-Path $tests 'difftest.cpp'), (Join-Path $blood 'XmlReaderLib.cpp'))
& $cl @flags @inc "/Fo$out\" /Fe"$exe" @src /link "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$winlib\ucrt\x64" "/LIBPATH:$winlib\um\x64"
if ($LASTEXITCODE -ne 0) { throw 'difftest compilation failed' }

Write-Host '[run] restoring test inputs...'
& (Join-Path $tests 'restore_data.ps1') | Out-Null

Write-Host '[run] running differential test...'
& $exe
$code = $LASTEXITCODE
Write-Host "[run] difftest exit code: $code"
exit $code
