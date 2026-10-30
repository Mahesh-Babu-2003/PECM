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
     Open firmware\Prepaid_RTC_sync.zip -> Prepaid_Interface_Test.uvproj,
     Rebuild all, then flash. This adds the "Sync RTC" support on the board.


CONNECTION SETTINGS (board defaults)
     RS485 port 1, Slave ID 1, 9600 baud, 8 data bits, Even parity, 1 stop bit.
     Connect adapter A/B to RS485_A_1 / RS485_B_1. If there is no reply, swap A/B.
