# Voltage Protector Plus — Proteus 9

**Abdullah Al Masud · AIUB · FSMB Recruitment 2026 Phase 1**

This submission contains the candidate's native Proteus project, matching 9600-baud firmware,
a one-page design report, wiring reference, demonstration guide and recorded evidence.
Two potentiometers emulate voltage and current, and a 5 V green LED represents the load.
Start with **START_HERE_BANGLA.txt**.

## Open and run

1. Extract the entire ZIP to a local folder.
2. Open `project_files/Protection_Device.pdsprj` in Proteus 9.
3. Double-click U1, select `project_files/voltage_protector_proteus.hex` as its Program File,
   and retain **16 MHz**. The supplied project contains an absolute firmware path from
   the original computer, so select the included HEX once and save the project.
4. Set RV1 near 77% and RV2 near 20%. Confirm approximately 231 V and 2 A on the LCD.
   RV3 controls LCD contrast. Leave all four momentary buttons released/open.
5. Start simulation. With default settings and correctly operating contacts, the load
   remains OFF during qualification, then turns ON after 3 healthy seconds.

The native `.pdsprj` contains its schematic internally; a separate `.dsn` is unnecessary.
It is copied byte-for-byte from the uploaded project, with a filename without spaces.
No circuit connections or native project properties have been edited in this package.

## Protection and recovery

| Setting | Default | Accepted range |
|---|---:|---|
| VMIN | 180 V | 50–240 V |
| VMAX | 250 V | 200–300 V; at least 20 V above VMIN |
| IMAX | 5 A | 0.5–9.9 A |
| DELAY | 3 s | Integer 1–30 s |

The firmware trips when V < VMIN, V > VMAX, or I > IMAX. Equality alone does not trip.
Recovery requires VMIN + 5 V ≤ V ≤ VMAX − 5 V and I ≤ IMAX − 0.2 A, continuously for
the effective delay. A new disturbance restarts qualification. Default recovery limits
are 185–245 V and no more than 4.8 A. ADC inputs emulate 0–300 V and 0–10 A;
they are independent test inputs, not measured AC RMS voltage or load current.

Intended contact wiring is 5 V → MAIN COM/NO → BACKUP COM/NO → load resistor/LED → GND.
A2 observes the main NO node using a 10k pulldown. HIGH energizes each NPN driver;
LOW commands OFF. The wiring reference describes the required topology, rather than
certifying every connection in the supplied native schematic.

## Three practical additions

- **Main contact feedback and backup isolation:** a sustained mismatch between command
  and main feedback latches LOCK. On a fault, the firmware commands the backup OFF
  before the main, without waiting for diagnosis. This can isolate a stuck-closed main
  contact if the backup contact and series wiring operate correctly.
- **Repeated-trip handling:** three distinct ON-to-fault trips inside a rolling 60 s
  window latch LOCK. After the second trip, recovery waits at least 10 s or the configured
  delay, whichever is longer. One sustained fault counts once.
- **Eight-event history:** `LOG` reports the newest eight RAM events, oldest first,
  with timestamps, readings, active limits, cause and trip count. The history resets at boot.

These additions make actuator failure and recovery behavior visible in a simple circuit.
No claim of an unprecedented invention is made.

## Local controls and indicators

SET advances through VMAX → IMAX → DELAY → VMIN → normal screen and SAVE.
UP/DOWN change voltage by 5 V, current by 0.1 A and delay by 1 s per press.
All buttons are normally OPEN and connect their input to GND when pressed;
firmware enables internal pullups. Press and release; a held button counts once.

| State | Intended load indication | Red fault LED |
|---|---|---|
| ON | Green ON | OFF |
| WAIT | Green OFF | OFF |
| FAULT | Green OFF | ON |
| LOCK | Green OFF | ON |

ACK only clears LOCK after safe recovery readings and settled LOW main feedback.
It starts a fresh recovery delay; it cannot force connection during a fault.
SAVE, DEFAULTS and settings changes do not clear a lock. Settings and lock reason use
EEPROM; the event history and rolling trip timestamps use RAM.

## Computer commands

For the native computer-control demonstration, connect a Proteus Virtual Terminal:
MCU PD1/TXD **pin 31 → terminal RXD**; terminal TXD **→ MCU PD0/RXD pin 30**.
Configure **9600 baud, 8 data bits, no parity, 1 stop bit**, with no flow control.
The supplied firmware has `PROTEUS_PROFILE` enabled. Do not select the 115200-baud
Wokwi binary when using this terminal configuration.

| Command | Purpose |
|---|---|
| `STATUS` | Readings, state, command, feedback, trip count and settings |
| `SET VMIN 180` | Lower voltage limit |
| `SET VMAX 250` | Upper voltage limit |
| `SET IMAX 5.0` | Current limit in amperes |
| `SET DELAY 3` | Recovery delay in seconds |
| `SAVE` | Disconnect and save settings to EEPROM |
| `DEFAULTS` | Restore defaults in RAM; use SAVE to retain |
| `ACK` | Request safe lock release |
| `LOG` | Read the last eight recorded events |
| `HELP` | Display command list |

Send one command per line. Letter case is ignored; CR, LF and CRLF endings work.
`cmd` is the intended relay state; `fb` measures only the main contact.
LOG `t` is milliseconds since boot; `V10`, `VMIN10`, `VMAX10` are tenths of a volt;
`I` and `IMAX` are milliamps. Cause bits are 1=UV, 2=OV, 4=OC, 8=main stuck closed,
16=main failed to close and 32=ACK recovery-margin/settling condition. Bits can combine.
Lock codes: 0=unlocked, 1=three trips, 2=stuck closed, 3=failed to close, 4=invalid EEPROM lock record.

## Evidence and limits

Included Proteus screenshots dated 10 October 2026 show firmware/LCD operation, local
settings, OVERCURRENT OFF in an earlier schematic revision and OVERVOLTAGE OFF in the
latest supplied screenshot. At 252.1 V / 2.00 A the latter shows red ON, green OFF and
relay levers in their OFF positions. Screenshots do not prove the final netlist or a full
ON-to-OFF sequence. The project was not executed in Proteus in this workspace.

Existing software evidence includes **45 host checks and 8 AVR HEX execution checks for
the Proteus firmware profile**. The original logs also include the Wokwi profile,
totalling 90 host and 16 HEX checks. Host peripherals are mocked; AVR tests use functional
ADC/contact/button models. These are firmware checks, not native Proteus or hardware tests.
Logs retain their original paths from the earlier shared project; the Proteus HEX in this
package is the same binary. See `test_evidence/build_manifest.json` for current hashes.

Native relay transitions, UART commands, repeated-trip lockout and contact-fault injection
still need recorded verification. Use `test_evidence/manual_test_plan.md`; record actual
observations before marking any further case PASS. Only the main contact has feedback.
No physical mains test, AC sensing calibration or measured mechanical disconnection time is claimed.

## Files and rebuilding

| Path | Purpose |
|---|---|
| `src/voltage_protector/voltage_protector.ino` | Proteus-profile Arduino source |
| `design_document.pdf` | One-page Proteus design report |
| `project_files/documents/design_document.docx` | Editable report |
| `project_files/Protection_Device.pdsprj` | Original native Proteus project |
| `project_files/voltage_protector_proteus.hex` | Compiled AVR application firmware |
| `schematics/wiring.md` | 32-pin MCU mapping and intended connections |
| `schematics/block_diagram.svg` | Editable conceptual block diagram |
| `project_files/COMPONENTS_BANGLA.txt` | Simple component explanations |
| `project_files/demo_script_bangla.txt` | Short demonstration sequence |
| `test_evidence/` | Native screenshots, software logs and manual test plan |

To rebuild, open the `.ino` in its `voltage_protector` folder in Arduino IDE, select
**Arduino Uno**, install **LiquidCrystal**, and export the compiled application binary.
EEPROM is supplied by the Arduino AVR core. Keep `#define PROTEUS_PROFILE 1` and use
the application HEX without a bootloader, with the MCU model at 16 MHz.
A source edit does not update the supplied HEX automatically.

## Submission

For a standalone Proteus repository, upload the contents of this folder, preserving
the root layout above. Review the report in your own words and retain one page. Record your
own demonstration of at most 5 minutes and at most 100 MB. Publish the required public
GitHub repository and enter its complete HTTPS URL in the assessment form.
This package does not include a recorded candidate video or perform those submission actions.
