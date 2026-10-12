@echo off
cd /d "%~dp0"
python tests\play_game.py
if errorlevel 1 pause
