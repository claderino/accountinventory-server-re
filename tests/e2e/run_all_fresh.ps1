$ErrorActionPreference='SilentlyContinue'
$out = 'C:\serverside_re\accountinventory\tests\e2e\all_fresh_results.txt'
Set-Content -Path $out -Value ''
function Restart-Test {
    Get-Process | Where-Object { $_.Path -like '*oracle*AccountInventoryDaemon.exe' -or $_.Path -like '*accountinventory\AccountInventoryDaemon\build*' -or $_.ProcessName -eq 'console_launch' } | Stop-Process -Force
    Start-Sleep -Seconds 2
    Start-Process -FilePath 'C:\serverside_re\accountinventory\analysis\oracle\console_launch.exe' -ArgumentList '"C:\serverside_re\accountinventory\analysis\oracle\AccountInventoryDaemon.exe"','--text','--conf=C:\serverside_re\accountinventory\analysis\oracle' -WindowStyle Hidden -RedirectStandardOutput 'C:\serverside_re\accountinventory\analysis\oracle\stdout.log' -RedirectStandardError 'C:\serverside_re\accountinventory\analysis\oracle\stderr.log' | Out-Null
    Start-Process -FilePath 'C:\serverside_re\accountinventory\run\test\console_launch.exe' -ArgumentList '"C:\serverside_re\accountinventory\AccountInventoryDaemon\build\AccountInventoryDaemon.exe"','--text','--conf=C:\serverside_re\accountinventory\run\test' -WindowStyle Hidden -RedirectStandardOutput 'C:\serverside_re\accountinventory\run\test\stdout.log' -RedirectStandardError 'C:\serverside_re\accountinventory\run\test\stderr.log' | Out-Null
    Start-Sleep -Seconds 8
}
$env:PYTHONIOENCODING='utf-8'
foreach ($suite in @('compare_oracle.py','compare_remaining.py','compare_world.py')) {
    Restart-Test
    $r = python "C:\serverside_re\accountinventory\tests\e2e\$suite" 2>&1
    Add-Content -Path $out -Value "=== $suite ==="
    Add-Content -Path $out -Value $r
}
Add-Content -Path $out -Value 'DONE'
