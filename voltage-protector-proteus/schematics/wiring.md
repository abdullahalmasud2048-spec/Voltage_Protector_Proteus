# Proteus wiring reference

The supplied native project uses the **32-pin ATMEGA328P symbol**, shown in the candidate's
screenshots. The numbers below refer to that symbol. Arduino D/A numbering selects firmware
pins; Proteus numbers select MCU terminals. Use the port names to confirm the mapping.
This reference states intended connections; the uploaded circuit is preserved unchanged.

| Function | Arduino | MCU port | Proteus 32-pin symbol |
|---|---|---|---:|
| Voltage pot wiper RV1 | A0 | PC0 / ADC0 | 23 |
| Current pot wiper RV2 | A1 | PC1 / ADC1 | 24 |
| Main NO feedback | A2 | PC2 / ADC2 | 25 |
| ACK button to GND | A3 | PC3 | 26 |
| Main NPN driver | D8 | PB0 | 12 |
| Backup NPN driver | D10 | PB2 | 14 |
| Red LED through 330 ohms | D13 | PB5 | 17 |
| SET button to GND | D7 | PD7 | 11 |
| UP button to GND | D6 | PD6 | 10 |
| DOWN button to GND | D9 | PB1 | 13 |
| LCD RS, LCD pin 4 | D12 | PB4 | 16 |
| LCD E, LCD pin 6 | D11 | PB3 | 15 |
| LCD D4, LCD pin 11 | D5 | PD5 | 9 |
| LCD D5, LCD pin 12 | D4 | PD4 | 2 |
| LCD D6, LCD pin 13 | D3 | PD3 | 1 |
| LCD D7, LCD pin 14 | D2 | PD2 | 32 |
| TX to Virtual Terminal RXD | D1 | PD1 / TXD | 31 |
| RX from Virtual Terminal TXD | D0 | PD0 / RXD | 30 |
| Reset pullup | RESET | PC6 / RESET | 29 |
| Analog supply | — | AVCC | 18 |
| ADC reference decoupling | — | AREF | 20 |

## Power and inputs

- Model clock: 16 MHz. The uploaded project already stores `CLOCK=16Mhz`.
- VCC is supplied by the model's hidden power pins and the VCC rail. The project rail
  definition sets VCC/VDD to 5 V and GND to 0 V. Keep a common GND and supply AVCC.
- RESET has a 10k pullup to 5 V. Use 100nF supply decoupling to GND. AREF can use 100nF
  to GND with the default AVCC ADC reference.
- RV1 and RV2 ends go to 5 V and GND; wipers go to PC0 and PC1. Their values emulate
  voltage and current independently. RV1 near 77% gives about 231 V; RV2 near 20% gives about 2 A.

## LCD and buttons

LCD VSS/pin 1 → GND; VDD/pin 2 → 5 V; VEE/pin 3 → RV3 contrast wiper;
RW/pin 5 → GND. RV3 ends go to 5 V/GND. D0–D3 are unused in 4-bit mode.
LCD RS, E and D4–D7 follow the table. Contrast is usually set near GND.
Each button connects its MCU input to GND when pressed and is normally OPEN.
Internal INPUT_PULLUP supplies its idle HIGH state; no extra button pullup is required.

## Relay drivers and contacts

For each relay: MCU drive pin → 1k resistor → 2N2222 base; emitter → GND;
collector → coil negative; coil positive → 5 V. A 10k base-to-GND resistor keeps the
driver OFF at startup. Across the coil, 1N4007 **cathode → 5 V, anode → collector**.
Select a 5 V relay model and check its activation threshold in the component properties.
HIGH energizes the driver. Do not identify coil behavior from the relay label alone.

Both COM/NO contacts must be in series:

**5 V → MAIN COM → MAIN NO → BACKUP COM → BACKUP NO → 330 ohms → green LED anode;
green LED cathode → GND.** Both NC contacts remain unused.

Connect MAIN NO to **PC2/A2, pin 25**, with a **10k pulldown to GND**.
Keep this feedback node separate from the backup driver **PB2/D10, pin 14**.
BACKUP COM belongs to the main NO node, rather than a GND node.
Connect red LED: PB5/D13 pin 17 → 330 ohms → anode; cathode → GND.

In the pictured relay orientation, COM is the lower contact terminal, NO is upper-left
and NC is upper-right. Confirm contact motion when running: OFF touches NC; ON touches NO.
Wire by function if the symbol is rotated or mirrored.

## Optional demonstrations

For computer commands, add a Virtual Terminal and cross TX/RX as in the table; set
9600 / 8N1. For a welded-main demonstration, add a normally open switch across **MAIN
COM and NO only**, leaving the backup contact in series. Keep it open for normal operation.
Neither optional demonstration is claimed as recorded native Proteus verification yet.
