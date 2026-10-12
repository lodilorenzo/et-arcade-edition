@echo off
rem Build, package and play the portable release in one step. Extra arguments go to
rem tools\play_portable.py (e.g. --no-build) and then to Play-ET.exe.
cd /d "%~dp0"
python tools\play_portable.py %*
if errorlevel 1 pause
