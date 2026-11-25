@echo off
rem drop-in replacement for D:\service\AccountInventoryDaemon\bin\
start "" "%~dp0AccountInventoryDaemon.exe" --text --conf="%~dp0"
