# Start proxy, oracle and our daemon, then run differential tests.
$ErrorActionPreference = 'SilentlyContinue'
Get-Process | Where-Object { $_.Path -like '*oracle*AccountInventoryDaemon.exe' -or $_.Path -like '*accountinventory\AccountInventoryDaemon\build*' -or $_.ProcessName -eq 'console_launch' } | Stop-Process -Force
Start-Sleep -Seconds 2
Start-Process -FilePath 'C:\Program Files\Python311\python.exe' -ArgumentList 'C:\serverside_re\accountinventory\tests\e2e\np_proxy.py','C:\serverside_re\accountinventory\tests\e2e\np_capture.log' -WindowStyle Hidden | Out-Null
Start-Sleep -Seconds 1
Start-Process -FilePath 'C:\serverside_re\accountinventory\analysis\oracle\console_launch.exe' -ArgumentList '"C:\serverside_re\accountinventory\analysis\oracle\AccountInventoryDaemon.exe"','--text','--conf=C:\serverside_re\accountinventory\analysis\oracle' -WindowStyle Hidden -RedirectStandardOutput 'C:\serverside_re\accountinventory\analysis\oracle\stdout.log' -RedirectStandardError 'C:\serverside_re\accountinventory\analysis\oracle\stderr.log' | Out-Null
Start-Process -FilePath 'C:\serverside_re\accountinventory\run\test\console_launch.exe' -ArgumentList '"C:\serverside_re\accountinventory\AccountInventoryDaemon\build\AccountInventoryDaemon.exe"','--text','--conf=C:\serverside_re\accountinventory\run\test' -WindowStyle Hidden -RedirectStandardOutput 'C:\serverside_re\accountinventory\run\test\stdout.log' -RedirectStandardError 'C:\serverside_re\accountinventory\run\test\stderr.log' | Out-Null
Start-Sleep -Seconds 12
$procs = Get-Process | Where-Object { $_.ProcessName -match 'AccountInventoryDaemon|console_launch' }
$procs | Select-Object Id,ProcessName,Path
$env:PYTHONIOENCODING='utf-8'
python C:\serverside_re\accountinventory\tests\e2e\compare_oracle.py
