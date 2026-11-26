$ErrorActionPreference = 'SilentlyContinue'
$base = 'C:\serverside_re\accountinventory'
Get-Process | Where-Object { $_.Path -like '*oracle*AccountInventoryDaemon.exe' -or $_.Path -like '*accountinventory\AccountInventoryDaemon\build*' -or $_.ProcessName -eq 'console_launch' } | Stop-Process -Force
Start-Sleep -Seconds 2

$oracleLogs = Join-Path $base 'analysis\oracle\log'
$oursLogs   = Join-Path $base 'run\test\log'
Get-ChildItem $oracleLogs -File | ForEach-Object { $_.FullName } | Set-Content (Join-Path $base 'analysis\oracle_logs_before.txt')
Get-ChildItem $oursLogs -File | ForEach-Object { $_.FullName } | Set-Content (Join-Path $base 'analysis\ours_logs_before.txt')

Start-Process -FilePath (Join-Path $base 'analysis\oracle\console_launch.exe') -ArgumentList '"C:\serverside_re\accountinventory\analysis\oracle\AccountInventoryDaemon.exe"','--text','--conf=C:\serverside_re\accountinventory\analysis\oracle' -WindowStyle Hidden -RedirectStandardOutput (Join-Path $base 'analysis\oracle\stdout.log') -RedirectStandardError (Join-Path $base 'analysis\oracle\stderr.log') | Out-Null
Start-Process -FilePath (Join-Path $base 'run\test\console_launch.exe') -ArgumentList '"C:\serverside_re\accountinventory\AccountInventoryDaemon\build\AccountInventoryDaemon.exe"','--text','--conf=C:\serverside_re\accountinventory\run\test' -WindowStyle Hidden -RedirectStandardOutput (Join-Path $base 'run\test\stdout.log') -RedirectStandardError (Join-Path $base 'run\test\stderr.log') | Out-Null
Start-Sleep -Seconds 125
Get-Process | Where-Object { $_.Path -like '*oracle*AccountInventoryDaemon.exe' -or $_.Path -like '*accountinventory\AccountInventoryDaemon\build*' -or $_.ProcessName -eq 'console_launch' } | Stop-Process -Force
Start-Sleep -Seconds 1

$beforeOracle = Get-Content (Join-Path $base 'analysis\oracle_logs_before.txt')
$beforeOurs   = Get-Content (Join-Path $base 'analysis\ours_logs_before.txt')
$newOracle = Get-ChildItem $oracleLogs -File | Where-Object { $beforeOracle -notcontains $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$newOurs   = Get-ChildItem $oursLogs -File | Where-Object { $beforeOurs -notcontains $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
'ORACLE_NEW=' + $newOracle.FullName
'OURS_NEW=' + $newOurs.FullName
'ORACLE_LINES=' + (Get-Content $newOracle.FullName | Measure-Object).Count
'OURS_LINES=' + (Get-Content $newOurs.FullName | Measure-Object).Count
Copy-Item $newOracle.FullName (Join-Path $base 'analysis\idle_oracle.log') -Force
Copy-Item $newOurs.FullName (Join-Path $base 'analysis\idle_ours.log') -Force
