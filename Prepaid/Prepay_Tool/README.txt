PREPAY_V0 MODBUS BRIDGE TOOL  -  setup on another PC
=====================================================

WHAT IS IN THIS ZIP
  prepaid_tool.py      the tool (GUI)
  modbus_rtu.py        Modbus RTU layer (must stay next to prepaid_tool.py)
  requirements.txt     Python package list (pyserial)
  run_tool.bat         start the tool with Python (installs pyserial if missing)
  build_exe.bat        build a single PrepayTool.exe
  firmware\            RTC-sync firmware changes for your Keil project
      Prepaid_RTC_sync.zip   full Keil project with the RTC sync already added
      rtc_sync.patch         the same change as a patch
      src\rs485_test.c, src\main.c   the two edited source files

The dashboard lists the latest five tamper events recorded by this tool
(newest first). Its history is saved on this PC at
%LOCALAPPDATA%\PrepayTool\tamper_events.json. If the tool first connects when
the device counter is already above zero, it shows that many prior event
numbers; their earlier timestamps are marked unavailable.
Register 40041 already clears the tamper count; the board requires SW4 to be
secure before accepting the clear command. A successful clear also resets the
tool's saved event history. No additional register is needed.
When an error is shown on the board, press SW3 five times
to clear the relay, RTC, EEPROM, and meter-communication error indicators.
An active fault can be reported again after the next device check.
The tool's Connect command can manually connect the loads at zero or negative
balance, except while the board's overload reconnect delay is active. The
overload countdown is displayed only for an overload trip; ordinary manual or
balance disconnects do not show that timer. Register 40086 reports whether
the overload reconnect delay is active.
Manual Disconnect latches the loads off. A later recharge or consumption/balance
change does not reconnect them; use Connect to turn them on again. Register
40073 supports 0 = automatic balance control, 1 = force connected, and
2 = force disconnected.


WHAT TO INSTALL ON THE OTHER PC
-------------------------------
A) To USE the tool (pick ONE way)

   Way 1 - with the .exe (nothing else to install)
     1. Build PrepayTool.exe once on any Windows PC with Python (see B below),
        then copy only PrepayTool.exe to the other PC.
     2. Install the driver of your USB-to-RS485 adapter (see C).
     3. Double-click PrepayTool.exe.

   Way 2 - with Python
     1. Install Python 3.9 or newer from https://www.python.org/downloads/
        (tick "Add python.exe to PATH"; keep "tcl/tk" ticked - it is the default).
     2. Install the adapter driver (see C).
     3. Double-click run_tool.bat  (it installs pyserial the first time).

B) To BUILD the .exe (only once, on a PC with Python)
     Double-click build_exe.bat -> PrepayTool.exe appears in this folder.

C) USB-to-RS485 adapter driver
     Install the driver for the chip in your adapter (CH340/CH341, FTDI FT232,
     CP210x, ...). Check Device Manager > Ports (COM & LPT) to see its COM number,
     then select that COM port in the tool.

D) To rebuild / flash the FIRMWARE (only if you change the board code)
     - Keil uVision with the C51 toolchain
     - Nuvoton MS51 device support (the project targets MS51PC0AE)
     - Nu-Link driver (the project is set up for a Nu-Link programmer)
     Open the workspace's Prepaid_Interface_Test.uvproj, rebuild all, then
     flash. This uses the current sources in ..\src, including the latest
     relay-balance and five-press error-reset behavior. The bundled
     firmware\Prepaid_RTC_sync.zip is an older RTC-sync snapshot and does not
     contain those later changes.
     The board RTC keeps time while powered off only if its backup supply is
     present. The firmware also saves the synchronized time in EEPROM as a
     recovery snapshot, but EEPROM cannot count elapsed time while power is off.


CONNECTION SETTINGS (board defaults)
     RS485 port 1, Slave ID 1, 9600 baud, 8 data bits, Even parity, 1 stop bit.
     Connect adapter A/B to RS485_A_1 / RS485_B_1. If there is no reply, swap A/B.
     The status indicator shows CONNECTED only after a valid board response.
     If the COM port is open but the board does not reply, it shows
     NOT COMMUNICATING; the button remains Disconnect until the port is closed.
