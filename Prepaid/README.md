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
- Communication settings use one baud-code register per port:

  | Holding register | Meaning | Values |
  | --- | --- | --- |
  | `40004` | Port 1 baud code | `0` = 4800, `1` = 9600, `2` = 14400, `3` = 19200, `4` = 38400, `5` = 56000, `6` = 57600, `7` = 115200 |
  | `40005` | Port 1 parity | `0` = none, `1` = even, `2` = odd |
  | `40006` | Port 1 controller ID | `1`–`247` |
  | `40007` | Port 2 baud code | Same code table as `40004` |
  | `40008` | Port 2 parity | `0` = none, `1` = even, `2` = odd |
  | `40009` | Port 2 meter ID | `1`–`247` |

  Read the current settings with Modbus function `03` (Read Holding Registers),
  starting at `40004` for six registers (zero-based PDU offset `3`, quantity
  `6`). The response contains the active code, parity, and IDs. A baud change
  takes effect after the write response is sent. For example, write `7` to
  `40004` to select 115200 baud.
  Valid settings are saved with a CRC in the AT24C64 EEPROM starting at address
  `0x0060` and restored at startup. Prepaid settings and balance data are saved
  separately in the AT24C64 EEPROM. Previously saved two-register baud values
  are converted to the new baud codes during startup migration.
- Controller holding-register map (the addresses below are Modbus register labels;
  use the matching zero-based protocol offsets in masters that request offsets):

  | Holding register(s) | Meaning | Access and format |
  | --- | --- | --- |
  | `40001`–`40003` | Relay 1–3 commands | Read/write: `0` = off, `1` = on, subject to balance control |
  | `40004`–`40009` | Port 1 and Port 2 communication settings | See the six-register table above |
  | `40010`–`40014` | Company name | Read/write, up to 10 ASCII characters, two per register, high byte first; default `PEMC` |
  | `40015` | Recharge top-up / remaining balance | Each write adds that unsigned 16-bit top-up to stored credit; reads return the signed 16-bit calculated balance |
  | `40016` | Mains kWh rate | Read/write unsigned integer |
  | `40017` | Mains kVAh rate | Read/write unsigned integer |
  | `40018` | DG kWh rate | Read/write unsigned integer |
  | `40019` | DG kVAh rate | Read/write unsigned integer |
  | `40020` | Solar kWh rate | Read/write unsigned integer |
  | `40021` | Solar kVAh rate | Read/write unsigned integer |
  | `40022` | Low-balance threshold | Read/write unsigned integer |
  | `40023` | Low-balance alert | Read-only: `1` when balance is negative or at/below the threshold; otherwise `0` |
  | `40024` | Backlight timeout | Read/write seconds; `0` keeps the backlight on |
  | `40025`–`40027` | Mains, DG, and Solar cumulative consumption | Read/write unsigned totals |
  | `40028` | Tamper event count | `0` when clear; increments from `1` to `5`, then wraps to `1`; write `0` to clear when SW4 is secure |
  | `40029` | Billing parameter selection | Read/write: `0` = kWh, `1` = kVAh, `2` = PF, `3` = kW, `4` = kVA, `5` = kVArh |
  | `40030` | All-relays-disconnected status | Read-only: `1` when all three commanded relay states are off; `0` otherwise |
  | `40031` | Load limit | Read/write unsigned integer in mode `0`; forced to `0` in mode `1` |
  | `40032` | Time limit | Read/write unsigned integer in mode `1`; forced to `0` in mode `0` |
  | `40033` | Disconnect mode | Read/write: `0` = load mode, `1` = time mode; selecting a mode clears the inactive limit |
  | `40034` | BMS relay override | Read/write and saved: `0` = automatic balance control; `1` = at zero/negative balance, allow BMS relay commands and preserve current relay states |
  | `40035` | Consumption after disconnection | Firmware latches `1` when a cumulative consumption total rises while balance is at/below zero and all relays are off; BMS writes `0` to acknowledge/clear |
  | `40036` | RTC year setting | Write `2000`–`2199` |
  | `40037` | RTC month setting | Write `1`–`12` |
  | `40038` | RTC day setting | Write `1`–`31` |
  | `40039` | RTC hour setting | Write `0`–`23` |
  | `40040` | RTC minute setting | Write `0`–`59` |
  | `40041` | RTC second setting | Write `0`–`59` |
  | `40042` | RTC synchronization command | Write `1` to apply staged date/time |
  | `40043` | RTC synchronization status | Read-only: `0` = idle, `1` = success, `2` = invalid date or RTC write failure |
  | `40044` | Live RTC year | Read-only |
  | `40045` | Live RTC month | Read-only |
  | `40046` | Live RTC day | Read-only |
  | `40047` | Live RTC hour | Read-only |
  | `40048` | Live RTC minute | Read-only |
  | `40049` | Live RTC second | Read-only |
  | `40050` | Live RTC validity | Read-only: `1` = valid time, `0` = RTC time unavailable |

  For selection `0`, the balance is calculated as `total credited amount -
  (Mains cumulative consumption × Mains kWh rate + DG cumulative consumption ×
  DG kWh rate + Solar cumulative consumption × Solar kWh rate)`. Selection `1`
  uses the corresponding kVAh rates. PF, kW, kVA, and kVArh selections are
  available in the register and LCD selector, but the DOCX map does not define
  their meter source registers or tariffs; while one is selected, the firmware
  holds the last calculated balance. The result is negative
  when calculated consumption cost exceeds the total credited amount. Write a
  top-up amount to `40015`; each write adds that amount to previously stored
  credit, and the firmware returns the calculated remaining balance in that
  same register. Writes to `40015`, rate writes,
  and cumulative-consumption writes trigger recalculation. The result is signed
  16-bit, clamped to `-32768` through `32767`; configure Modscan to display
  `40015` as signed 16-bit to see negative balances. Rates, consumption, and
  recharge input use unsigned integer values without decimal scaling. The
  low-balance alert is `1` when the balance is negative or at or below the
  configured threshold, otherwise `0`. While this alert is active, the buzzer
  sounds for half a second three times, with half-second gaps, and starts the
  next burst every 30 seconds. It stops when the alert clears. Relay control follows the calculated
  balance sign when `40034 = 0`: a positive balance turns all three relays on,
  while zero or a negative balance turns them off and blocks relay-on commands.
  SW4 is the
  active-low tamper input on P2.4: a pressed switch
  is secure, and a released switch is tampered. A debounced transition to
  tampered increments the event count at `40028`, shows a tamper alert on the
  LCD, and sounds the buzzer once. The
  counter cycles through `1` to `5`, then returns to `1` on the next event. It
  remains unchanged when the switch returns to secure. Write `0` to `40028`
  while SW4 is secure to clear it. At startup, a secure input sets the
  count to `0`; a tampered input initializes it to `1`. The Modbus master
  should poll `40028` to detect events. The count is held in RAM and is not
  saved across a firmware restart.
- The schematic uses latching relay SET/RESET coils. Firmware pulses the
  selected coil for 120 ms. Software relay states start at zero and update when
  commanded. The board has no relay contact feedback, so readback is not a
  measurement of the physical contact. At startup, firmware synchronizes the
  relays with the calculated balance. Register `40030` reflects these commanded
  states; a mixed relay state reports `0` because not all three are off.
  With `40034 = 1`, low-balance automation preserves the current relay states
  and allows the BMS to issue relay commands; it does not turn relays on by
  itself. Register `40035` latches if a mains, DG, or solar cumulative total
  increases through a write to the cumulative registers `40025`–`40027` while
  all relays are off at zero or negative balance. The flag is saved across
  restarts, and the BMS can clear it by writing `0` to `40035`.
- The LCD shows two IEEE-754 floats with two decimal places: `F1` combines
  meter value 2 as the high 16-bit word with meter value 1 as the low word; `F2`
  combines meter value 4 high with meter value 3 low. This is the 16-bit
  word-swapped format. The display reads the retained meter array directly;
  meter values are served at `40201` through `40250`. Values loaded from the
  onboard EEPROM are shown at startup; without a saved value, the
  display starts at zero.
- The LCD rotates every three seconds through company name, remaining
  balance/alert, Mains/DG/Solar load states, rates, meter values, RTC, and the
  current tamper count from `40028`. The
  RTC page shows `DATE: DD:MM:YYYY` and `TIME: HH:MM:SS`; an invalid RTC read
  shows `RTC NOT SYNCED` until the BMS sets the clock through Modbus registers
  `40036`–`40042`. From a non-billing page, press SW1 three times, with no more
  than 2.5 seconds between presses, to keep the RTC page on screen. Press SW2
  three times to resume normal page rotation.
- The billing page shows one parameter at a time: current balance/top-up
  (`40015`), the six kWh/kVAh rates (`40016`–`40021`), low-balance threshold
  (`40022`), backlight timeout (`40024`), consumption totals (`40025`–`40027`),
  billing parameter selection (`40029`), load limit (`40031`), time limit
  (`40032`), and disconnect mode (`40033`). Press SW1 on that page
  to enter edit mode (`*` appears), then press SW1 to select the next parameter,
  SW2 to decrease it, or SW3 to increase it by one. For the first item, SW2/SW3
  set a pending top-up amount; hold SW1 for about five seconds to apply that
  credit and exit. For other items, changes take effect immediately. These
  parameters use the same registers as Modbus and are saved with the prepaid
  settings. The LCD shows the current balance for `40015` outside edit mode.
- Synchronize the clock from the BMS by writing decimal year, month, day, hour,
  minute, and second values to `40036`–`40041`, then writing `1` to `40042`.
  The BMS can send `40036`–`40042` together in one function 16 request, or stage
  the six values and issue a function 6 write to `40042`. Read `40043` for the
  result: `0` = idle, `1` = success, `2` = invalid date/time or RTC write
  failure. Time is local and uses a 24-hour clock. Successful synchronization
  also attempts to save a backup snapshot to EEPROM. SW1–SW3 no longer edit or
  save the RTC. Read live year, month, day, hour, minute, and second from
  `40044`–`40049`; `40050` reports whether the RTC value is valid.
- Hold SW2 and SW3 together for about five seconds to show the active RS-485
  settings. The LCD shows Port 1's baud rate, parity, and slave ID for three
  seconds, then Port 2's settings for three seconds, before returning to the
  previous display page.
- Each debounced switch press gives a short buzzer beep. Successful Modbus clock
  synchronization writes an RTC snapshot to EEPROM address `0x0040`, separate
  from the meter record at `0x0100`, and reads it back to verify the save. If
  the RTC is invalid at startup, firmware restores the most recently saved
  snapshot. This restores the last saved time; it cannot advance the clock
  during an outage.
- A failed or timed-out meter poll clears the live receive array, while the 50
  values served at `40201` through `40250` remain at their last successfully
  received or restored values. The controller registers are unaffected.
- The LCD is driven by onboard 74HC595 U3: serial data is on P2.1, shift clock
  on P2.2, and latch on P2.3. The global `back_light_on` variable controls its
  active-low PNP backlight switch: `0` is off and `1` is on. Register `40024`
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
  active-low tamper switch on P2.4. If the RTC and EEPROM both have no valid
  time, the LCD reports that the RTC needs synchronization from the BMS. The
  buzzer is driven on P3.4.

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
  balance control permits. Read tamper event count at `40028`. Use
  `40004` through `40009` to configure both UARTs (write a baud code, such as
  `7` for 115200), then reconnect the PC master
  using the new port 1 settings. To synchronize the clock, write the six
  decimal time fields to `40036`–`40041`, write `1` to `40042`, then read the
  result from `40043`. Read `40044`–`40049` to monitor the live RTC and `40050`
  to check its validity.
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
  indices 0–2 are relay commands, 3–8 are the two baud-code/parity/ID groups,
  and 9–32 follow the application map above.
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
