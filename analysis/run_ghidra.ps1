# Run Ghidra headless on the unpacked AccountInventoryDaemon.exe: import + analyze + decompile all functions.
param([switch]$Reanalyze, [string]$Prototypes = "")
$an = "C:\serverside_re\accountinventory\analysis"
$env:JAVA_HOME = "C:\serverside_re\_tools\jdk25\jdk-25.0.5.1+2"
$gh = "C:\serverside_re\_tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat"
$File = "$an\mem\AccountInventoryDaemon.exe.mem.exe"
$Prefix = "AccountInventoryDaemon"
$proj = "$an\ghidra_proj"
$tsv = "$an\functions.tsv"
$out = "$an\decomp"
$progName = [IO.Path]::GetFileName($File)
New-Item -ItemType Directory -Force -Path $proj, $out | Out-Null
$common = @("$proj", "proj", "-scriptPath", "$an\ghidra", "-max-cpu", "8")
$proto = @()
if ($Prototypes -ne "" -and (Test-Path $Prototypes)) { $proto = @($Prototypes) }
if (-not $Reanalyze -and (Test-Path "$proj\proj.gpr")) {
    & $gh @common -process $progName -noanalysis `
        -postScript ExportDecomp.java $out $tsv @proto 2>&1 | ForEach-Object { "$_" }
} else {
    & $gh @common -import $File -overwrite `
        -postScript ExportDecomp.java $out $tsv @proto 2>&1 | ForEach-Object { "$_" }
}
