# PREPAY_V0 Modbus energy bridge

This Keil C51/uVision firmware for the MS51PC0AE polls an energy meter on
RS-485 port 2 and serves the controller's 50-register map plus a 50-register
meter mirror as a Modbus RTU slave on RS-485 port 1. It also controls three
latching relays and exposes independent
communication settings for both RS-485 ports.
The onboard 16x2 LCD, driven through 74HC595 U3, cycles through prepaid status,
word-swapped meter values, and the PCF8563 RTC date and time.

## Modbus and LCD data

- Default energy meter settings on port 2: slave ID `4`, `9600 baud`, `8E1`.
- Default controller slave settings on port 1: ID `1`, `9600 baud`, `8E1`.
- The meter poll starts at protocol offset `0` and requests 50 registers.
- The controller map supports holding registers `40001` through `40050`
  (zero-based protocol offsets `0` through `49`). The three relay controls start
  at `40001`, `40002`, and `40003` for relays 1, 2, and 3. Write `0` for
  OFF or `1` for ON using function `06`; function `03` reads back the commanded
  state. Function `16` (`0x10`, Write Multiple Registers) can also write a
  contiguous block, including relay commands.
- The meter's 50 holding-register values are exposed read-only at `40201`
  through `40250` (zero-based protocol offsets `200` through `249`). Read them
  with function `03`; writes to this range are rejected. Values retain the last
  successful reading, including values restored from the onboard EEPROM.
- Communication settings follow the relay registers. Configure them
  by writing the port 1 slave's holding registers:

  | Holding register | Meaning | Values |
  | --- | --- | --- |
  | `40004` | Port 1 baud, high 16 bits | 32-bit unsigned baud value, high word first |
  | `40005` | Port 1 baud, low 16 bits | Supported baud values listed below |
  | `40006` | Port 1 parity | `0` = none, `1` = even, `2` = odd |
  | `40007` | Port 1 slave ID | `1`–`247` |
  | `40008` | Port 2 baud, high 16 bits | 32-bit unsigned baud value, high word first |
  | `40009` | Port 2 baud, low 16 bits | Supported baud values listed below |
  | `40010` | Port 2 parity | `0` = none, `1` = even, `2` = odd |
  | `40011` | Port 2 meter slave ID | `1`–`247` |

  Read the current settings with Modbus function `03` (Read Holding Registers),
  starting at `40004` for eight registers (zero-based PDU offset `3`, quantity
  `8`). The reply reports the active baud, parity, and slave IDs for both ports;
  a baud value is returned high word first. If a baud change is being written
  one word at a time, reads continue to report the active rate until the new
  high/low pair is valid and applied.

  Supported baud rates are `4800`, `9600`, `14400`, `19200`, `38400`,
  `56000`, `57600`, and `115200`. Write `40004`/`40005` or `40008`/`40009`
  together with function `16` (`0x10`) when setting a two-word baud value. For example,
  `115200` is high word `1`, low word `49664` (`0xC200`). A low baud value
  such as `9600` is high word `0`, low word `9600`. For function `06`, write
  the high word and low word separately; the UART changes when the complete
  pair forms a supported rate. The response to a port 1 setting write is sent
  with the current connection settings before the new settings take effect.
  Valid settings are saved with a CRC in the AT24C64 EEPROM starting at address
  `0x0060` and restored at startup. Prepaid settings and balance data in
  `40012`–`40026` are saved separately in the AT24C64 EEPROM.
- Prepaid holding-register map (the addresses below are Modbus register labels;
  use the matching zero-based protocol offsets in masters that request offsets):

  | Holding register(s) | Meaning | Access and format |
  | --- | --- | --- |
  | `40012`–`40016` | Company name | Read/write, up to 10 ASCII characters, two per register, high byte first; default `PEMC` |
  | `40017` | Recharge input / remaining balance | Write an unsigned 16-bit total recharge amount; reads return the signed 16-bit calculated balance |
  | `40018` | Mains unit rate | Read/write unsigned integer |
  | `40019` | DG unit rate | Read/write unsigned integer |
  | `40020` | Solar unit rate | Read/write unsigned integer |
  | `40021` | Low-balance threshold | Read/write unsigned integer |
  | `40022` | Low-balance alert | Read-only: `1` when balance is negative or at/below the threshold; otherwise `0` |
  | `40023` | Backlight timeout | Read/write seconds; `0` keeps the backlight on |
  | `40024` | Mains cumulative consumption | Read/write unsigned integer total |
  | `40025` | DG cumulative consumption | Read/write unsigned integer total |
  | `40026` | Solar cumulative consumption | Read/write unsigned integer total |
  | `40027` | BMS relay override | Read/write: `0` = automatic balance control; `1` = force all relays on |
  | `40028` | Tamper status (SW4) | Read-only: `0` = pressed/not tampered; `1` = released/tampered |
  | `40029`–`40050` | Reserved | — |

  The balance is calculated as `total recharge amount - (Mains cumulative
  consumption × Mains unit rate + DG cumulative consumption × DG unit rate +
  Solar cumulative consumption × Solar unit rate)`. The result is negative
  when calculated consumption cost exceeds the total recharge amount. Write
  the total recharge amount to `40017`, and the firmware returns the calculated
  remaining balance in that same register. Writes to `40017`, unit-rate writes,
  and cumulative-consumption writes trigger recalculation. The result is signed
  16-bit, clamped to `-32768` through `32767`; configure Modscan to display
  `40017` as signed 16-bit to see negative balances. Rates, consumption, and
  recharge input use unsigned integer values without decimal scaling. The
  low-balance alert is `1` when the balance is negative or at or below the
  configured threshold, otherwise `0`. Relay control follows the calculated
  balance sign: a positive balance turns all three relays on, while zero or a
  negative balance turns them off and blocks relay-on commands. Write `1` to
  `40027` to force all relays on even when the balance is zero or negative;
  write `0` to restore automatic balance control. The override is not saved
  across a restart. SW4 is the active-low tamper input on P2.4: a pressed switch
  reports `0` (not tampered), and a released switch reports `1` (tampered) in
  `40028`.
- The schematic uses latching relay SET/RESET coils. Firmware pulses the
  selected coil for 120 ms. Software relay states start at zero and update when
  commanded. The board has no relay contact feedback, so readback is not a
  measurement of the physical contact. At startup, firmware synchronizes the
  relays with the calculated balance and override register.
- The LCD shows two IEEE-754 floats with two decimal places: `F1` combines
  meter value 2 as the high 16-bit word with meter value 1 as the low word; `F2`
  combines meter value 4 high with meter value 3 low. This is the 16-bit
  word-swapped format. The display reads the retained meter array directly;
  meter values are served at `40201` through `40250`. Values loaded from the
  onboard EEPROM are shown at startup; without a saved value, the
  display starts at zero.
- The LCD rotates every three seconds through company name, remaining
  balance/alert, Mains/DG/Solar load states, rates, meter values, and RTC. The
  RTC page shows `DATE: DD:MM:YYYY` and `TIME: HH:MM:SS`; an invalid RTC read
  shows an error until editing starts.
- The board switches set the RTC while its page is selected. SW1 moves the
  blinking cursor digit through day tens/ones, month tens/ones, year
  thousands/hundreds/tens/ones, hour tens/ones, minute tens/ones, and second
  tens/ones, then wraps to the first digit. SW2 decreases and SW3 increases the
  selected digit. Each accepted change is written to the RTC immediately;
  impossible dates and out-of-range times are ignored. Editing keeps the RTC
  page visible and the LCD cursor blinking; after 30 seconds without a press,
  the display resumes cycling through its pages.
- Hold SW2 and SW3 together for about five seconds to show the active RS-485
  settings. The LCD shows Port 1's baud rate, parity, and slave ID for three
  seconds, then Port 2's settings for three seconds, before returning to the
  previous display page.
- Each debounced switch press gives a short buzzer beep. Hold SW1 for about five
  seconds to save the current valid RTC date/time to the AT24C64 EEPROM. A
  success/failure message is shown and a successful save gives two confirmation
  beeps. The RTC snapshot uses EEPROM address `0x0040`, separate from the meter
  record at `0x0100`, and is read back to verify the save. If the RTC is invalid
  at startup, firmware restores the saved snapshot from EEPROM before entering
  the date editor. This restores the last saved time; it cannot advance the
  clock during an outage.
- A failed or timed-out meter poll clears the live receive array, while the 50
  values served at `40201` through `40250` remain at their last successfully
  received or restored values. The controller registers are unaffected.
- The LCD is driven by onboard 74HC595 U3: serial data is on P2.1, shift clock
  on P2.2, and latch on P2.3. The global `back_light_on` variable controls its
  active-low PNP backlight switch: `0` is off and `1` is on. Register `40023`
  controls the idle timeout; zero keeps the backlight on. Local button presses
  and changed Modbus values wake it. The LCD data and RS bits are set while
  Enable is low before Enable is pulsed.
- The GPIO-controlled status LEDs on P3.0 and P1.7 are held off. Four RS-485
  indicator LEDs are connected directly to the TX/RX signals and can still
  illuminate during bus activity.
- The PCF8563 RTC is read once per second. `rtc_datetime` holds the decoded
  date and time; `rtc_datetime_valid` is cleared if I2C fails or the RTC's
  voltage-low flag says its time may be invalid. The three tactile switches
  are active-low inputs on P3.5 (SW1), P3.2 (SW2), and P3.1 (SW3). SW4 is the
  active-low tamper switch on P2.4. If the RTC
  and EEPROM both have no valid time, the editor starts from 01:01:2000
  00:00:00; adjust it with the switches to write a valid time. The buzzer is
  driven on P3.4.

The PCF8563's 32.768 kHz crystal keeps time only while the RTC has power. The
schematic also shows a CR2032 backup battery for the RTC; fit a good battery
with correct polarity to keep the clock running when the main supply is off.

## Wiring

1. Connect the energy meter A/B pair to `RS485_A_2` / `RS485_B_2`.
2. Connect the PC Modbus adapter A/B pair to `RS485_A_1` / `RS485_B_1`.
3. Join signal grounds when available. Do not connect the meter supply to A/B.
4. Configure the meter for ID `4`, `9600 8E1`; configure the PC master for
  controller ID `1`, `9600 8E1`, and function `03` (Read Holding Registers).
  Read `40001` through `40050` for controller data and `40201` through `40250`
  for meter readings. Use function `06` to write
  `40001` through `40003` with `0` or `1` to operate relays 1 through 3 when
  balance control permits; write `1` to `40027` to force all three on. Read
  tamper status at `40028`. Use
  `40004` through `40011` to configure both UARTs, then reconnect the PC master
  using the new port 1 settings.
5. Keep each bus daisy-chained and terminate only its physical ends.

## Keil live-debug watch values

Open `Prepaid_Interface_Test.uvproj` and start Debug mode to inspect the meter
and slave state:

- `energy_meter_connected`: `1` after a valid meter response; `0` at startup or
  after a meter exception/timeout.
- `energy_meter_data_valid`: `1` when the live receive array has a valid
  response; otherwise `0`.
- `energy_meter_successful_responses`: increments for each valid meter response.
- `energy_meter_registers[0]` through `[49]`: latest raw meter values.
- `s_energy_meter_registers[0]` through `[49]`: retained last-good meter
  values served at holding registers `40201` through `40250`.
- `slave_holding_registers[0]` through `[49]`: holding registers 40001–40050;
  indices 0–2 are relay commands, indices 3–10 configure port 1 and port 2
  baud/parity/IDs, and indices 11–27 follow the prepaid map above.
- `slave_relay_states[0]` through `[2]`: commanded states for registers
  40001–40003.
- `back_light_on`: `0` turns the LCD backlight off; `1` turns it on. Changes
  while running take effect immediately. `BACK_LIGHT_OFF` and `BACK_LIGHT_ON`
  are provided as named values.
- `port1_valid_request_count` and `port1_response_count`: valid slave requests
  received and responses sent on port 1.

## Build

Open `Prepaid_Interface_Test.uvproj` in Keil uVision and build target
`PREPAY_V0_Modbus_Bridge`. The HEX and BIN are generated in `Output`; flash the
new HEX to the controller after building.
