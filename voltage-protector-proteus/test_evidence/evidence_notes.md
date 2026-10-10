# Recorded evidence

The native screenshots were supplied by the candidate on 10 October 2026.

| File | What can be observed | Scope |
|---|---|---|
| 01_proteus_overvoltage_off.png | LCD 252.1 V / 2.00 A, OVERVOLTAGE OFF, red ON, green OFF; relay levers OFF | Latest supplied schematic screenshot; one operating point |
| 02_proteus_local_settings.png | VMAX 250.0 V settings page, WAIT and SET-next instruction | Local settings screen; no complete edit/save sequence |
| 03_proteus_overcurrent_earlier_revision.png | Approximately 150.1 V / 8.00 A and OVERCURRENT OFF | Earlier schematic revision; input also below VMIN; not a standalone OC isolation test |

The supplied project was inspected as a valid ZIP-based Proteus container. ROOT.DSN and
project rail definitions are embedded. Its MCU program path is an absolute Windows path;
the quick start explains selecting the bundled HEX. The native project bytes were preserved.
Structural checks do not validate native circuit connectivity or execute the simulation.

`host_test_results.txt` and `avr_hex_test_results.txt` are unchanged earlier software
logs across both firmware profiles. The Proteus profile passed 45 host checks and 8
machine-code checks. Mocked host peripherals and functional AVR contact/ADC models
do not verify the native Proteus schematic. Original log paths refer to the earlier
shared package; binary identity is recorded in this package's build manifest.

No newly recorded native UART, normal-to-fault contact transition, repeated-trip or
contact-injection result is included. No physical mains or relay timing measurement is claimed.
