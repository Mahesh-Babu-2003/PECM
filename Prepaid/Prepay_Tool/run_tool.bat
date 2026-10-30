@echo off
REM Runs the tool from Python (no .exe needed). Double-click.
cd /d "%~dp0"
where python >nul 2>nul
if errorlevel 1 (
    echo Python not found. Install Python 3.9+ from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH".
    pause
    exit /b 1
)
python -c "import serial" >nul 2>nul || python -m pip install -r requirements.txt
python prepaid_tool.py
if errorlevel 1 pause
