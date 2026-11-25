$p = Start-Process -FilePath 'C:\serverside_re\ranking\XmlReader_sv64\tests\difftest.exe' -PassThru -RedirectStandardOutput 'C:\serverside_re\ranking\XmlReader_sv64\tests\diff.out' -RedirectStandardError 'C:\serverside_re\ranking\XmlReader_sv64\tests\diff.err'
if (-not $p.WaitForExit(15000)) { $p.Kill(); Write-Host 'TIMEOUT' } else { Write-Host ('EXIT=' + $p.ExitCode) }
