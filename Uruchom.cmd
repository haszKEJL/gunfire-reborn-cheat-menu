@echo off
cd /d "%~dp0"
bin\ScarletAim.exe
if errorlevel 1 pause
