# Native Proteus demonstration plan

These are expected results to check in the supplied circuit, not recorded passes.
Use default settings, 16 MHz, 5 V rails and the included 9600-baud HEX. Leave buttons
open except while pressing. Use LCD readings rather than assuming exact pot percentages.

| Test | Input or action | Expected observation | Current evidence |
|---|---|---|---|
| Normal connection | Approximately 231 V / 2 A | WAIT, then ON after 3 healthy seconds; green ON | Full final-circuit transition not recorded |
| Over-voltage | Raise V above 250 V | OV OFF; green OFF; red ON; both relays commanded OFF | Latest screenshot: 252.1 V / 2.00 A |
| Under-voltage | Lower V below 180 V | UV OFF; green OFF; red ON | Not recorded natively |
| Over-current | Raise I above 5 A | OC OFF; green OFF; red ON | Earlier schematic screenshot: about 150.1 V / 8.00 A; UV was also present |
| Recovery | Restore approximately 231 V / 2 A | Continuous healthy delay, then ON | Not recorded natively |
| Local settings | Press SET; edit with UP/DOWN; advance to exit | Value changes and settings save | VMAX page visible; full edit/save sequence not recorded |
| UART | Virtual Terminal 9600/8N1; send STATUS, SET VMAX 255, SAVE, LOG | Readings, changed setting, save and event history | Not recorded natively |
| Repeated trips | Three distinct ON-to-fault transitions inside 60 s | First normal delay; second ≥10 s; third LOCK | Not recorded natively |
| Safe ACK | Fault removed, feedback LOW; press ACK | LOCK clears; fresh delay; then ON | Not recorded natively |
| Main stuck closed | Switch across MAIN COM–NO only; create voltage/current fault | Backup isolates; persistent feedback mismatch causes LOCK | Not recorded natively; requires injection switch |
| Main fails to close | Interrupt main coil/control temporarily | Sustained mismatch causes LOCK and backup OFF | Not recorded natively |
| EEPROM | Save setting, reset MCU while retaining EEPROM | Setting and saved lock reason retained | Not recorded natively |

Capture the before/after state and UART text where useful. For contact tests, confirm the
green LED load is actually in series with both NO contacts and A2 reads MAIN NO rather
than a relay drive net. Record simulation timings separately from physical measurements.
Return the circuit to normal, restore defaults and save when finished.
