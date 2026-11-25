# Generates the UTF-16 / BOM test inputs (run once; files are checked in).
$dir = 'C:\serverside_re\ranking\XmlReader_sv64\tests\data'
$text = "<?xml version=`"1.0`" encoding=`"UTF-16`"?>`r`n<root a='1'><child b=`"2`"/><child b='3'/></root>`r`n"
$enc = New-Object System.Text.UnicodeEncoding($false, $true)  # UTF-16LE with BOM
[System.IO.File]::WriteAllBytes((Join-Path $dir 'utf16.xml'), $enc.GetPreamble() + $enc.GetBytes($text))
$encNoBom = New-Object System.Text.UnicodeEncoding($false, $false)
[System.IO.File]::WriteAllBytes((Join-Path $dir 'utf16nobom.xml'), $encNoBom.GetBytes($text))
$utf8 = New-Object System.Text.UTF8Encoding($true)
[System.IO.File]::WriteAllBytes((Join-Path $dir 'bom.xml'), $utf8.GetBytes("<?xml version='1.0' encoding='UTF-8'?>`n<root a='ok'/>`n"))
