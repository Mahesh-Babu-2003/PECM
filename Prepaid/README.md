# PREPAY_V0 Modbus energy bridge

This Keil C51/uVision firmware for the MS51PC0AE polls an energy meter on
RS-485 port 2 and serves the controller's 151-register map plus a 50-register
meter mirror as a Modbus RTU slave on RS-485 port 1. It also controls three
latching relays and exposes independent
communication settings for both RS-485 ports.
The onboard 16x2 LCD, driven through 74HC595 U3, cycles through the customer ID
and selected prepaid load and balance values.

## Modbus and LCD data

- Default energy meter settings on port 2: slave ID `4`, `9600 baud`, `8E1`.
- Default controller slave settings on port 1: ID `1`, `9600 baud`, `8E1`.
- The meter poll starts at protocol offset `0` and requests 50 registers.
- The controller map supports offsets `0`–`150` (`40001`–`40151`). Offsets
  `85`–`150` (`40086`–`40151`) are reserved, read as zero, and reject writes.
  Offsets `151`–`199` are unassigned and reads are rejected. Since Modbus
  function `03` reads at most 125 registers per request, read the controller
  map in two requests: offsets `0`–`124`, then `125`–`150`. Write `1` to
  `40001` to disconnect all three relays, or `0` to connect all three when
  balance control permits. `40001` is the only relay command register;
  physical relay states are not exposed as individual holding registers.
  Function `16` can write the global relay command along with other contiguous
  registers.
- The meter's 50 holding-register values are exposed read-only at `40201`
  through `40250` (zero-based protocol offsets `200` through `249`). Read them
  with function `03`; writes to this range are rejected. Values retain the last
  successful reading, including values restored from the onboard EEPROM.
- Communication settings use one baud-rate register per port. Write the actual
  rate (for example, `9600`) instead of a code:

  | Holding register | Meaning | Values |
  | --- | --- | --- |
  | `40002` | Port 1 baud rate | `4800`, `9600`, `14400`, `19200`, `38400`, `56000`, `57600`; use `65535` for `115200` |
  | `40003` | Port 1 parity | `0` = none, `1` = even, `2` = odd |
  | `40004` | Port 1 controller ID | `1`–`247` |
  | `40005` | Port 2 baud rate | Same values as `40002` |
  | `40006` | Port 2 parity | `0` = none, `1` = even, `2` = odd |
  | `40007` | Port 2 meter ID | `1`–`247` |

  Read the current settings with Modbus function `03` (Read Holding Registers),
  starting at `40002` for six registers (zero-based PDU offset `1`, quantity
  `6`). The response contains the active baud rates, parity, and IDs. A baud
  change takes effect after the write response is sent. For example, write
  `9600` to `40002` to select 9600 baud. Since a Modbus register holds only 16
  bits, use `65535` as the special value for 115200 baud.
  Valid settings are saved with a CRC in the AT24C64 EEPROM starting at address
  `0x0060` and restored at startup. Prepaid settings and balance data are saved
  separately in the AT24C64 EEPROM. Previously saved baud formats are converted
  to the current actual-rate register values during startup migration.
- Controller holding-register map (the addresses below are Modbus register labels;
  use the matching zero-based protocol offsets in masters that request offsets):

  | Holding register(s) | Meaning | Access and format |
  | --- | --- | --- |
  | `40001` | All-load command/status | Read/write: `1` = disconnect all three relays; `0` = connect all three, subject to balance control. The read value is `1` only when all relays are disconnected. |
  | `40002`–`40007` | Port 1 and Port 2 communication settings | See the six-register table above |
  | `40008`–`40012` | Company name | Read/write, up to 10 alphanumeric ASCII characters, two per register, high byte first; default `PEMC` |
  | `40013` | Remaining balance | Read-only signed 16-bit calculated balance |
  | `40014` | Mains kWh rate | Read/write unsigned integer |
  | `40015` | Mains kVAh rate | Read/write unsigned integer |
  | `40016` | DG kWh rate | Read/write unsigned integer |
  | `40017` | DG kVAh rate | Read/write unsigned integer |
  | `40018` | Solar kWh rate | Read/write unsigned integer |
  | `40019` | Solar kVAh rate | Read/write unsigned integer |
  | `40020` | Low-balance threshold | Read/write unsigned integer |
  | `40021` | Low-balance alert | Read-only: `1` when balance is negative or at/below the threshold; otherwise `0` |
  | `40022` | Backlight timeout | Read/write seconds; `0` keeps the backlight on |
  | `40023` | Mains kWh consumption, old | Read-only previous meter reading |
  | `40024` | Mains kWh consumption, new | Read/write; the difference from `40023` is shown in `40025` |
  | `40025` | Mains kWh consumption difference | Read-only latest increase (`new - old`); internal cumulative consumption is retained separately |
  | `40026` | Mains kVAh consumption, old | Read-only previous meter reading |
  | `40027` | Mains kVAh consumption, new | Read/write; the difference from `40026` is shown in `40028` |
  | `40028` | Mains kVAh consumption difference | Read-only latest increase (`new - old`); internal cumulative consumption is retained separately |
  | `40029` | DG kWh consumption, old | Read-only previous meter reading |
  | `40030` | DG kWh consumption, new | Read/write; the difference from `40029` is shown in `40031` |
  | `40031` | DG kWh consumption difference | Read-only latest increase (`new - old`); internal cumulative consumption is retained separately |
  | `40032` | DG kVAh consumption, old | Read-only previous meter reading |
  | `40033` | DG kVAh consumption, new | Read/write; the difference from `40032` is shown in `40034` |
  | `40034` | DG kVAh consumption difference | Read-only latest increase (`new - old`); internal cumulative consumption is retained separately |
  | `40035` | Solar kWh consumption, old | Read-only previous meter reading |
  | `40036` | Solar kWh consumption, new | Read/write; the difference from `40035` is shown in `40037` |
  | `40037` | Solar kWh consumption difference | Read-only latest increase (`new - old`); internal cumulative consumption is retained separately |
  | `40038` | Solar kVAh consumption, old | Read-only previous meter reading |
  | `40039` | Solar kVAh consumption, new | Read/write; the difference from `40038` is shown in `40040` |
  | `40040` | Solar kVAh consumption difference | Read-only latest increase (`new - old`); internal cumulative consumption is retained separately |
  | `40041` | Tamper event count | `0` when clear; increments from `1` to `5`, then wraps to `1`; write `0` to clear when SW4 is secure |
  | `40042` | Billing parameter selection | Read/write: `0` = kWh billing, `1` = kVAh billing, `2` = PF, `3` = kW, `4` = kVA, `5` = kVArh |
  | `40043` | All-loads-disconnected status | Read-only: `1` when all three commanded loads are off; `0` otherwise |
  | `40044` | Max-load threshold | Read/write unsigned threshold in the selected `40047` measurement units; `0` disables overload cycling |
  | `40045` | Max-load delay | Read/write seconds, timed against the RTC, that the selected load value must remain above `40044`; `0` trips immediately |
  | `40046` | Reconnect delay | Read/write RTC-timed minutes disconnected after a trip before reconnecting and checking the load again; `0` reconnects immediately |
  | `40047` | Load-parameter selection | Read/write: `0` = sum of internally accumulated kWh consumption, `1` = sum of internally accumulated kVAh consumption, `3` = meter register `40201` value |
  | `40048` | Consumption after disconnection | Firmware latches `1` when a cumulative reading increases while balance is at/below zero and all loads are disconnected; BMS writes `0` to acknowledge/clear |
  | `40049`–`40054` | Live RTC | Read-only year, month, day, hour, minute, second; set all six in one function 16 request. Invalid time returns exception `03`; RTC or EEPROM write failure returns `04`; an active LCD editor returns `06` |
  | `40055` | Last recharge amount | Read-only signed 16-bit value from the most recent recharge input |
  | `40056` | Recharge input | Write a signed 16-bit adjustment: positive adds credit, negative subtracts credit; `0` adds no credit and does not reset billing |
  | `40057` | Total amount deducted | Read-only selected-mode cost since the latest nonzero recharge, clamped to 65535; resets to `0` on nonzero recharge |
  | `40058` | Clock synchronization | Write `1` to refresh the board RTC values in `40049`–`40054`; read `1` for success or `2` if the board RTC could not be read; write `0` to clear the status |
  | `40059` | Tamper timestamp valid | Read-only: `1` when `40060`–`40065` contain a saved timestamp; otherwise `0` |
  | `40060`–`40065` | Tamper timestamp | Read-only: year, month, day, hour, minute, second of the latest tamper event |
  | `40066` | Last-recharge timestamp valid | Read-only: `1` when `40067`–`40072` contain a saved timestamp; otherwise `0` |
  | `40067`–`40072` | Last-recharge timestamp | Read-only: year, month, day, hour, minute, second of the latest nonzero recharge |
  | `40073` | BMS load override | Read/write: `0` = automatic balance control, `1` = connect loads despite zero or negative balance; a positive recharge clears it automatically |
  | `40074` | Reset balance | Write `1` to reset the calculated balance to `0`; it reads back as `0` and retains internal cumulative consumption counters |
  | `40075` | Port 2 meter communication lost | Read-only: `0` after a valid meter response; `1` when the meter is not communicating, times out, or returns an invalid response |
  | `40076` | Relay error | Read-only error indicator |
  | `40077` | RTC error | Read-only error indicator |
  | `40078` | EEPROM error | Read-only error indicator |
  | `40079` | Last meter disconnect timestamp valid | Read-only: `1` when `40080`–`40085` contain a saved timestamp; otherwise `0` |
  | `40080`–`40085` | Last meter disconnect timestamp | Read-only: year, month, day, hour, minute, second when the latest meter communication loss was detected |
  | `40086`–`40151` | Reserved | Read as zero; writes are rejected |

  Register `40013` is the remaining balance. Recharge writes to `40056` add
  credit, and consumption cost is deducted according to the selected billing
  mode. Registers `40025`, `40028`, `40031`, `40034`, `40037`, and `40040`
  show only the latest meter increase, not a running total. The firmware keeps
  separate cumulative counters internally and uses them to calculate the
  balance. Register `40057` shows the selected-mode cost since the most recent
  nonzero recharge. A nonzero recharge resets it to `0` and clears only the
  selected billing mode's three internal source counters; the other mode's
  counters are unchanged. Writing `0` to `40056` does not change the balance or
  clear consumption; new consumption continues to be deducted.
  The accumulated credit is kept internally and is not exposed as a register.
  When `40042 = 0`, the deduction uses the internal cumulative kWh counters
  and rates (`40014`, `40016`, and `40018` for Mains, DG, and Solar). When
  `40042 = 1`, it uses the internal cumulative kVAh counters and rates
  (`40015`, `40017`, and `40019`). PF, kW, kVA, and kVArh
  selections have no corresponding consumption sources in this register map;
  while one is selected, the firmware holds the last calculated balance. The
  result is negative
  when calculated consumption cost exceeds the total credited amount. Write a
  signed recharge adjustment to `40056`; positive values add credit, negative
  values reduce credit, and `0` means no recharge. Register `40013` returns the
  signed remaining balance. Writes to `40056`, rate writes,
  and meter-reading writes trigger recalculation. The result is signed
  16-bit, clamped to `-32768` through `32767`; configure Modscan to display
  `40013` as signed 16-bit to see negative balances. Rates and consumption use
  unsigned integer values without decimal scaling; `40056` is signed 16-bit.
  Increasing a new meter reading triggers recalculation using only the
  difference from the previous reading, and the additional consumption cost
  further reduces the remaining balance. For example, readings `1200`, `1218`,
  and `1223` produce differences `0`, `18`, and `5` in the corresponding
  difference register; the balance accounting accumulates those increases
  internally. A balance of `-1653` followed by a recharge of `2000` becomes
  `347`; `40057` resets to `0` and then reports only subsequent billed
  consumption.
  Writing `1` to `40074` resets the calculated balance to zero while retaining
  the cumulative consumption totals.
  The low-balance alert is `1` when the balance is negative or at or below the
  configured threshold, otherwise `0`. While this alert is active, the buzzer
  sounds for half a second three times, with half-second gaps, and starts the
  next burst every 30 seconds. It stops when the alert clears. Relay control follows the calculated
  balance sign when `40073 = 0`: a positive balance connects all three loads,
  while zero or a negative balance disconnects them. Writing `1` to `40073`
  is the explicit BMS bypass that connects the loads at zero or negative
  balance; a subsequent positive recharge automatically clears the bypass.
  SW4 is the
  active-low tamper input on P2.4: a pressed switch
  is secure, and a released switch is tampered. A debounced transition to
  tampered increments the event count at `40041`, shows a tamper alert on the
  LCD, and sounds the buzzer once. The
  counter cycles through `1` to `5`, then returns to `1` on the next event. It
  remains unchanged when the switch returns to secure. Write `0` to `40041`
  while SW4 is secure to clear it. At startup, a secure input sets the
  count to `0`; a tampered input initializes it to `1`. The Modbus master
  should poll `40041` to detect events. The count is held in RAM and is not
  saved across a firmware restart. When the RTC time is valid, the latest
  tamper event's date and time are stored in EEPROM, shown on LCD screen 13,
  and exposed through `40059`–`40065`. A saved-time validity flag prevents an
  invalid RTC value from being reported as an event time.
- The schematic uses latching relay SET/RESET coils. Firmware pulses the
  selected coil for 120 ms. Software relay states start at zero and update when
  commanded. The board has no relay contact feedback, so readback is not a
  measurement of the physical contact. At startup, firmware synchronizes the
  relays with the calculated balance. Register `40043` reflects these commanded
  states; a mixed relay state reports `0` because not all three are off.
  A zero or negative balance disconnects all three loads unless the BMS has
  explicitly enabled the `40073` bypass. Register `40048` latches if a
  mains, DG, or solar cumulative total
  increases through a write to one of the new-reading registers `40024`,
  `40027`, `40030`, `40033`, `40036`, or `40039` while
  all relays are off at zero or negative balance. The flag is saved across
  restarts, and the BMS can clear it by writing `0` to `40048`.
- The LCD user interface is implemented in `src/lcd_ui.c`; the screen IDs and
  order are in `src/lcd_ui.h`. Screen 1 is customer ID, screen 2 balance, and
  screen 3 load status. Screens 4–9 show the six rates in order: Mains kWh,
  Mains kVAh, DG kWh, DG kVAh, Solar kWh, and Solar kVAh. Screen 10 is the
  low-balance alert (shown only when `40021 = 1`), screen 11 low-balance
  threshold, screen 12 Max Load/Max Time, screen 13 tamper event, screen 14 BMS
  relay override, screen 15 billing parameter, screen 16 last recharge, and
  screen 17 total amount deducted (`40057`), and screen 18 last meter
  communication loss time. Screens 13, 16, and 18 show their saved timestamp
  as `DD/MM/YY HH:MM` when available. The latest nonzero
  recharge stores its date and time in EEPROM with its own validity flag at
  `40059`–`40072`.
  The billing screen displays `0: kWh`, `1: kVAh`, `2: PF`, `3: kW`,
  `4: kVA`, or `5: kVArh`. The RTC editor is screen ID 0. Each customer screen
  stays visible for three seconds; a short SW1 press advances immediately. The
  load/time screen shows the max-load threshold from `40044`; `40045` and
  `40046` configure the overload trip and reconnect delays. Other screen data
  comes from customer ID `40008`–`40012`, balance `40013`, all-load status `40001`,
  consumption registers `40023`–`40040`, threshold `40020`, and total amount
  deducted `40057`.
- Set the RTC from the PCB buttons: press SW2 three times within two seconds
  to enter edit mode. SW1 selects each digit in `DD:MM:YYYY HH:MM:SS` order;
  SW2 increases the selected digit and SW3 decreases it when released. Hold
  SW3 for three seconds to write the edited time to the RTC and EEPROM and
  return to the customer-screen loop. Modbus registers `40049`–`40054` update
  to the saved date and time. Time uses a 24-hour clock. The registers expose
  read-only live year, month, day, hour, minute, and second. Recharge,
  synchronization, and saved-timestamp registers are `40055`–`40072`;
  `40073` is the BMS relay override, `40074` resets balance to zero, and
  `40075` reports Port 2 communication loss. `40076`–`40078` report relay,
  RTC, and EEPROM errors; `40079`–`40085` report the last meter-disconnect
  timestamp; and `40086`–`40151` are reserved.
  To synchronize the BMS clock to the board clock, the BMS writes `1` to
  `40058`, waits for status `1`, then reads the board date and time from
  `40049`–`40054` and sets
  its own RTC from those values. Status `2` means the board RTC read failed;
  write `0` to clear the status before or after a synchronization.
- Each debounced switch press gives a short buzzer beep. Saving the clock writes
  an RTC snapshot to EEPROM address `0x0040`, separate from the meter record at
  `0x0100`, and reads it back to verify the save. If the RTC is invalid at
  startup, firmware restores the most recently saved snapshot. This restores
  the last saved time; it cannot advance the clock during an outage.
- A failed or timed-out meter poll clears the live receive array, while the 50
  values served at `40201` through `40250` remain at their last successfully
  received or restored values. The controller registers are unaffected.
- The LCD is driven by onboard 74HC595 U3: serial data is on P2.1, shift clock
  on P2.2, and latch on P2.3. The global `back_light_on` variable controls its
  active-low PNP backlight switch: `0` is off and `1` is on. Register `40022`
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
  active-low tamper switch on P2.4. If the RTC has no valid time when edit mode
  is requested, the LCD reports an RTC read failure. The buzzer is driven on
  P3.4.

The PCF8563's 32.768 kHz crystal keeps time only while the RTC has power. The
schematic also shows a CR2032 backup battery for the RTC; fit a good battery
with correct polarity to keep the clock running when the main supply is off.

## Wiring

1. Connect the energy meter A/B pair to `RS485_A_2` / `RS485_B_2`.
2. Connect the PC Modbus adapter A/B pair to `RS485_A_1` / `RS485_B_1`.
3. Join signal grounds when available. Do not connect the meter supply to A/B.
4. Configure the meter for ID `4`, `9600 8E1`; configure the PC master for
  controller ID `1`, `9600 8E1`, and function `03` (Read Holding Registers).
  Read controller data from offsets `0`–`150` (`40001`–`40151`) in two
  function `03` requests (maximum 125 registers per request); offsets
  `151`–`199` are unassigned. Read `40201` through `40250` for meter readings.
  Use function `06` to write `1` to `40001` to disconnect
  all three relays or `0` to connect all three when balance control permits.
  Read tamper event count at `40041`. Use
  `40002` through `40007` to configure both UARTs (write the actual baud rate,
  such as `9600`; use `65535` for 115200), then reconnect the PC master
  using the new port 1 settings. The live RTC is available in read-only
  registers `40049`–`40054` (year, month, day, hour, minute, second); set it
  using the PCB SW1/SW2/SW3 editing flow described above.
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
- `slave_holding_registers[0]` through `[150]`: holding registers 40001–40151;
  index 0 is the all-relay command/status, indices 1–6 are the two serial-port
  settings groups, and later indices follow the map above.
- `slave_relay_states[0]` through `[2]`: commanded states of the three
  physical relays; individual relay states have no holding-register addresses.
- `back_light_on`: `0` turns the LCD backlight off; `1` turns it on. Changes
  while running take effect immediately. `BACK_LIGHT_OFF` and `BACK_LIGHT_ON`
  are provided as named values.
- `port1_valid_request_count` and `port1_response_count`: valid slave requests
  received and responses sent on port 1.

## Build

Open `Prepaid_Interface_Test.uvproj` in Keil uVision and build target
`PREPAY_V0_Modbus_Bridge`. The HEX and BIN are generated in `Output`; flash the
new HEX to the controller after building.
