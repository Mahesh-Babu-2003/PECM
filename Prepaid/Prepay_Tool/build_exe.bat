@echo off
REM ============================================================
REM  Builds PrepayTool.exe (single file, no Python needed to run)
REM  Put this file in the same folder as prepaid_tool.py and
REM  modbus_rtu.py, then double-click it.
REM  Needs Python 3.9+ installed (tick "Add python.exe to PATH").
REM ============================================================
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
    echo Python was not found. Install it from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH" during setup.
    pause
    exit /b 1
)

if not exist prepaid_tool.py (
    echo prepaid_tool.py not found in this folder.
    pause
    exit /b 1
)
if not exist modbus_rtu.py (
    echo modbus_rtu.py not found in this folder.
    pause
    exit /b 1
)

echo Creating build environment...
python -m venv build_env || goto :fail
call build_env\Scripts\activate.bat

echo Installing pyserial and pyinstaller...
python -m pip install --upgrade pip pyserial pyinstaller || goto :fail

echo Building PrepayTool.exe ...
pyinstaller --onefile --windowed --clean --name PrepayTool ^
    --hidden-import serial.tools.list_ports_windows ^
    prepaid_tool.py || goto :fail

echo.
echo ===== Done =====
echo Your program is: %~dp0dist\PrepayTool.exe
copy /y dist\PrepayTool.exe "%~dp0PrepayTool.exe" >nul
echo A copy was also placed next to this script: PrepayTool.exe
pause
exit /b 0

:fail
echo.
echo Build failed - see the messages above.
pause
exit /b 1
