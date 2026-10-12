@echo off
rem Compile and package the publishable release (no original ET data) into dist\.
rem Usage: Build-Release.bat [--version 1.0.0] [--no-build]
cd /d "%~dp0"
python tools\package.py %*
pause
