param([string]$File = "D:\service\AccountInventoryDaemon\bin\StsMsgSrv64.dll", [string]$Prefix = "StsMsg")
$an = "C:\serverside_re\accountinventory\analysis"
$env:JAVA_HOME = "C:\serverside_re\_tools\jdk25\jdk-25.0.5.1+2"
$gh = "C:\serverside_re\_tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat"
$proj = "$an\ghidra_proj_dll"
$out = "$an\decomp_dll"
$tsv = "$an\dll_${Prefix}_functions.tsv"
if (-not (Test-Path $tsv)) { Set-Content -Path $tsv -Value '' }
$progName = [IO.Path]::GetFileName($File)
New-Item -ItemType Directory -Force -Path $proj, $out | Out-Null
& $gh "$proj" "proj" -scriptPath "$an\ghidra" -max-cpu 4 -import $File -overwrite `
    -postScript ExportDecomp.java $out $tsv 2>&1 | ForEach-Object { "$_" }
