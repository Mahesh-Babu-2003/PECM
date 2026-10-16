# PREPAY_V0 Modbus energy bridge

This Keil C51/uVision firmware for the MS51PC0AE polls an energy meter on
RS-485 port 2 and serves 50 holding registers as a Modbus RTU slave on RS-485
port 1. It also controls three latching relays from separate Modbus registers.
The onboard 16x2 LCD, driven through 74HC595 U3, alternates between two
word-swapped meter floats and the PCF8563 RTC date and time.

## Modbus and LCD data

- Energy meter: slave ID `4`, `9600 baud`, `8E1`.
- Controller slave on port 1: ID `1`, `9600 baud`, `8E1`.
- The meter poll starts at protocol offset `0` and requests 4 registers.
- The slave map supports holding registers `40001` through `40050` (zero-based
  protocol offsets `0` through `49`). `40001` through `40004` mirror the four
  meter registers and are read-only. The three relay controls immediately
  follow at `40005`, `40006`, and `40007` for relays 1, 2, and 3. Write `0` for
  OFF or `1` for ON using function `06`; function `03` reads back the commanded
  state. The remaining registers `40008` through `40050` are additional holding
  registers; they start at zero and ModScan can set them with function `06`.
  They retain their values until reset.
- The schematic uses latching relay SET/RESET coils. Firmware pulses the
  selected coil for 120 ms. Software relay states start at zero and update when
  commanded. The board has no relay contact feedback, so readback is not a
  measurement of the physical contact. Relays are not forced to a state at
  startup.
- The LCD shows two IEEE-754 floats with two decimal places: `F1` combines
  `40002` as the high 16-bit word with `40001` as the low word; `F2` combines
  `40004` high with `40003` low. This is the 16-bit word-swapped format. The
  first four retained meter values are also the values served at `40001` through
  `40004`. Values loaded from the onboard EEPROM are shown at startup; without
  a saved value, the display starts at zero.
- The display alternates every three seconds to a date/time page:
  `DATE: DD:MM:YYYY` on row 1 and `TIME: HH:MM:SS` on row 2. If the RTC read is
  invalid, it shows an RTC error message until editing starts.
- The board switches set the RTC while its page is selected. SW1 moves the
  blinking cursor digit through day tens/ones, month tens/ones, year
  thousands/hundreds/tens/ones, hour tens/ones, minute tens/ones, and second
  tens/ones, then wraps to the first digit. SW2 decreases and SW3 increases the
  selected digit. Each accepted change is written to the RTC immediately;
  impossible dates and out-of-range times are ignored. Editing keeps the RTC
  page visible and the LCD cursor blinking; after 30 seconds without a press,
  the display resumes alternating pages.
- Each debounced switch press gives a short buzzer beep. Hold SW1 for about five
  seconds to save the current valid RTC date/time to the AT24C64 EEPROM. A
  success/failure message is shown and a successful save gives two confirmation
  beeps. The RTC snapshot uses EEPROM address `0x0040`, separate from the meter
  record at `0x0020`, and is read back to verify the save. If the RTC is invalid
  at startup, firmware restores the saved snapshot from EEPROM before entering
  the date editor. This restores the last saved time; it cannot advance the
  clock during an outage.
- A failed or timed-out meter poll clears the live receive array, while the
  four slave-served meter values remain at the last successfully received or
  restored values. The additional holding registers are unaffected.
- The LCD is driven by onboard 74HC595 U3: serial data is on P2.1, shift clock
  on P2.2, and latch on P2.3. The global `back_light_on` variable controls its
  active-low PNP backlight switch: `0` is off (the startup default), `1` is on.
  Changing it while running takes effect immediately. The LCD data and RS bits
  are set while Enable is low before Enable is pulsed.
- The GPIO-controlled status LEDs on P3.0 and P1.7 are held off. Four RS-485
  indicator LEDs are connected directly to the TX/RX signals and can still
  illuminate during bus activity.
- The PCF8563 RTC is read once per second. `rtc_datetime` holds the decoded
  date and time; `rtc_datetime_valid` is cleared if I2C fails or the RTC's
  voltage-low flag says its time may be invalid. The three tactile switches
  are active-low inputs on P3.5 (SW1), P3.2 (SW2), and P3.1 (SW3). If the RTC
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
  Read `40001` through `40050` for the data map. Use function `06` to write
  `40005` through `40007` with `0` or `1` to operate the relays, or write
  `40008` through `40050` as auxiliary registers.
5. Keep each bus daisy-chained and terminate only its physical ends.

## Keil live-debug watch values

Open `Prepaid_Interface_Test.uvproj` and start Debug mode to inspect the meter
and slave state:

- `energy_meter_connected`: `1` after a valid meter response; `0` at startup or
  after a meter exception/timeout.
- `energy_meter_data_valid`: `1` when the live receive array has a valid
  response; otherwise `0`.
- `energy_meter_successful_responses`: increments for each valid meter response.
- `energy_meter_registers[0]` through `[3]`: latest raw meter values.
- `slave_holding_registers[0]` through `[49]`: holding registers 40001–40050;
  indices 0–3 mirror the meter, indices 4–6 are relay commands, and indices
  7–49 are ModScan-writable auxiliary values.
- `slave_relay_states[0]` through `[2]`: commanded states for registers
  40005–40007.
- `back_light_on`: `0` turns the LCD backlight off; `1` turns it on. Changes
  while running take effect immediately. `BACK_LIGHT_OFF` and `BACK_LIGHT_ON`
  are provided as named values.
- `port1_valid_request_count` and `port1_response_count`: valid slave requests
  received and responses sent on port 1.

## Build

Open `Prepaid_Interface_Test.uvproj` in Keil uVision and build target
`PREPAY_V0_Modbus_Bridge`. The HEX and BIN are generated in `Output`; flash the
new HEX to the controller after building.
