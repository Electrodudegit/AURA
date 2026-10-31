@echo off
REM Local AURA companion (Windows). CPU/RAM plus Spotify window title as Now Playing.
REM First run: run_companion.bat --host 192.168.1.42 --token K7M2Q9XW
cd /d "%~dp0"
python aura_companion.py %*
