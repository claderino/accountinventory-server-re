# Restores test inputs after the ORIGINAL config reader rewrites/backs them up.
$dir = 'C:\serverside_re\ranking\XmlReader_sv64\tests\data'
Remove-Item (Join-Path $dir '*.bak') -ErrorAction SilentlyContinue
Remove-Item (Join-Path $dir 'empty.config.xml') -ErrorAction SilentlyContinue
Copy-Item (Join-Path $dir 'full.config.xml') (Join-Path $dir 'rankingd.config.xml') -Force
Set-Content -Path (Join-Path $dir 'malformed.xml') -Value '<?xml version="1.0" encoding="UTF-8"?>' -Encoding ASCII
Add-Content -Path (Join-Path $dir 'malformed.xml') -Value '<root><child></root>' -Encoding ASCII
$cfgDir = 'C:\serverside_re\ranking\XmlReader_sv64\tests\cfgdata'
$badDir = 'C:\serverside_re\ranking\XmlReader_sv64\tests\cfgbad'
New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
New-Item -ItemType Directory -Force -Path $badDir | Out-Null
Get-ChildItem $cfgDir -Force | Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $badDir -Force | Remove-Item -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $dir 'full.config.xml') 'C:\serverside_re\ranking\XmlReader_sv64\tests\cfgdata\rankingd.config.xml' -Force
Copy-Item (Join-Path $dir 'malformed.xml') 'C:\serverside_re\ranking\XmlReader_sv64\tests\cfgbad\malformed.xml' -Force
Get-ChildItem $dir | Select-Object Name, Length
