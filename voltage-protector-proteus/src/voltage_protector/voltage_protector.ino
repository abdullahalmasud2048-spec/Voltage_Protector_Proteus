#define PROTEUS_PROFILE 1
/*
 * Voltage Protector Plus - safe low-voltage SIMULATION, Arduino Uno.
 * A0: 0..1023 emulates 0..300 V. A1 emulates 0..10 A.
 * Two NO contacts in SERIES switch a 5 V LED load; NEVER connect mains.
 * A2 reads main contact feedback. A3 is the ACK button. D10 is backup relay.
 * Protection remains active during LCD menus and serial commands.
 */
#include <Arduino.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const uint8_t VOLTAGE_PIN = A0, CURRENT_PIN = A1;
const uint8_t RELAY_PIN = 8, BACKUP_PIN = 10, FEEDBACK_PIN = A2, ACK_PIN = A3, FAULT_LED_PIN = 13;
const uint8_t SET_PIN = 7, UP_PIN = 6, DOWN_PIN = 9;
#ifdef PROTEUS_PROFILE
// Ordinary low-side NPN coil drivers are active HIGH.
const uint8_t RELAY_ON_LEVEL = HIGH, RELAY_OFF_LEVEL = LOW;
const uint32_t SERIAL_BAUD = 9600;
#else
// Verified in Wokwi with transistor="npn": HIGH closes COM-NO.
const uint8_t RELAY_ON_LEVEL = HIGH, RELAY_OFF_LEVEL = LOW;
const uint32_t SERIAL_BAUD = 115200;
#endif
const uint32_t SAMPLE_MS = 5, DISPLAY_MS = 200, TELEMETRY_MS = 1000;
const uint32_t TRIP_WINDOW_MS = 60000, SECOND_TRIP_DELAY_MS = 10000;
const uint32_t RELAY_SETTLE_MS = 50, RELAY_MISMATCH_MS = 50;
const uint8_t LOCK_EEPROM_ADDRESS = 32;
const uint16_t VOLTAGE_MARGIN = 50; // 5.0 V, in tenths of a volt
const uint16_t CURRENT_MARGIN = 200; // 0.2 A, in milliamps
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

struct Settings {
  uint16_t vmin; // tenths of V
  uint16_t vmax; // tenths of V
  uint16_t imax; // mA
  uint8_t delaySeconds;
};
struct Button {
  uint8_t pin;
  uint8_t raw;
  uint8_t stable;
  uint32_t changedAt;
};
enum State : uint8_t { WAITING, CONNECTED, TRIPPED, LOCKED };
enum Fault : uint8_t { NO_FAULT = 0, UNDER_VOLTAGE = 1, OVER_VOLTAGE = 2, OVER_CURRENT = 4,
                      RELAY_STUCK_CLOSED = 8, RELAY_STUCK_OPEN = 16 };
enum LockReason : uint8_t { UNLOCKED = 0, REPEATED_TRIPS = 1, CONTACT_CLOSED = 2,
                           CONTACT_OPEN = 3, LOCK_RECORD_INVALID = 4 };
enum EventKind : uint8_t { BOOT_EVENT, ON_EVENT, TRIP_EVENT, REPEAT_LOCK_EVENT,
                          RELAY_LOCK_EVENT, ACK_EVENT, DENIED_EVENT, SET_EVENT, SAVE_EVENT };
struct Event {
  uint32_t at;
  uint16_t v, i, vmin, vmax, imax;
  uint8_t kind, cause, trips;
};

Settings settings = {1800, 2500, 5000, 3};
Button setButton = {SET_PIN, HIGH, HIGH, 0};
Button upButton = {UP_PIN, HIGH, HIGH, 0};
Button downButton = {DOWN_PIN, HIGH, HIGH, 0};
Button ackButton = {ACK_PIN, HIGH, HIGH, 0};
State state = WAITING;
LockReason lockReason = UNLOCKED;
uint8_t faultMask = NO_FAULT, menuPage = 0;
uint16_t voltage = 0, current = 0;
bool outputEnabled = false, qualifying = false;
uint32_t healthySince = 0, lastSample = 0, lastDisplay = 0, lastTelemetry = 0;
uint32_t outputChangedAt = 0, mismatchSince = 0, tripBackoffMs = 0;
bool mismatchActive = false;
uint32_t tripTimes[3];
uint8_t recentTrips = 0;
Event events[8], logSnapshot[8];
uint8_t eventHead = 0, eventCount = 0, logIndex = 0, logCount = 0;
char rxLine[48];
uint8_t rxLength = 0;
bool rxOverflow = false;
char txBuffer[256];
uint8_t txHead = 0, txTail = 0;

// Explicit prototypes avoid Arduino sketch preprocessor issues with custom types.
bool validSettings(const Settings &candidate);
bool pressed(Button &button, uint32_t now);
void protect(uint32_t now);
void recordEvent(EventKind kind, uint8_t cause, uint32_t now);

uint8_t txFree() { return (uint8_t)(txTail - txHead - 1); }
void queueText(const char *text) {
  size_t n = strlen(text);
  if (n > txFree()) return; // Drop a message, never block protection.
  while (*text) { txBuffer[txHead++] = *text++; }
}
void queueFlash(const __FlashStringHelper *text) {
  const char *p = reinterpret_cast<const char *>(text);
  size_t n = strlen_P(p);
  if (n > txFree()) return;
  char c;
  while ((c = (char)pgm_read_byte(p++)) != '\0') txBuffer[txHead++] = c;
}
void flushSerial() {
  uint8_t budget = 16;
  while (budget-- && txHead != txTail && Serial.availableForWrite() > 0) {
    Serial.write((uint8_t)txBuffer[txTail++]);
  }
}
void setOutput(bool enabled) {
  // On a fault, BACKUP opens first. No diagnostic wait delays isolation.
  if (enabled != outputEnabled) {
    outputChangedAt = (uint32_t)millis();
    mismatchActive = false;
  }
  if (!enabled) digitalWrite(BACKUP_PIN, RELAY_OFF_LEVEL);
  digitalWrite(RELAY_PIN, enabled ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  if (enabled) digitalWrite(BACKUP_PIN, RELAY_ON_LEVEL);
  outputEnabled = enabled;
}
const char *stateName() {
  if (state == LOCKED) return "LOCK";
  if (state == CONNECTED) return "ON";
  if (state == TRIPPED) return "FAULT";
  return "WAIT";
}
void queueStatus() {
  char line[120];
  snprintf(line, sizeof(line),
    "t=%lu V=%u.%u I=%u.%03u %s fault=%u cmd=%u fb=%u trips=%u lock=%u\n",
    (unsigned long)millis(), voltage / 10, voltage % 10,
    current / 1000, current % 1000, stateName(), faultMask, outputEnabled ? 1 : 0,
    digitalRead(FEEDBACK_PIN) == HIGH ? 1 : 0, recentTrips, (uint8_t)lockReason);
  queueText(line);
}
void queueSettings() {
  char line[100];
  snprintf(line, sizeof(line),
    "VMIN=%u.%u VMAX=%u.%u IMAX=%u.%03u DELAY=%u s\n",
    settings.vmin / 10, settings.vmin % 10, settings.vmax / 10, settings.vmax % 10,
    settings.imax / 1000, settings.imax % 1000, settings.delaySeconds);
  queueText(line);
}
bool validSettings(const Settings &candidate) {
  return candidate.vmin >= 500 && candidate.vmin <= 2400 &&
         candidate.vmax >= 2000 && candidate.vmax <= 3000 &&
         candidate.vmax >= candidate.vmin + 200 &&
         candidate.imax >= 500 && candidate.imax <= 9900 &&
         candidate.delaySeconds >= 1 && candidate.delaySeconds <= 30;
}
uint16_t checksum(const uint8_t *bytes, uint8_t count) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < count; i++) {
    crc ^= bytes[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
  }
  return crc;
}
bool loadSettings() {
  uint8_t bytes[12];
  for (uint8_t i = 0; i < 12; i++) bytes[i] = EEPROM.read(i);
  uint16_t stored = (uint16_t)bytes[10] | ((uint16_t)bytes[11] << 8);
  if (bytes[0] != 'V' || bytes[1] != 'P' || bytes[2] != 1 ||
      checksum(bytes, 10) != stored) return false;
  Settings candidate = {
    (uint16_t)(bytes[3] | ((uint16_t)bytes[4] << 8)),
    (uint16_t)(bytes[5] | ((uint16_t)bytes[6] << 8)),
    (uint16_t)(bytes[7] | ((uint16_t)bytes[8] << 8)), bytes[9]
  };
  if (!validSettings(candidate)) return false;
  settings = candidate;
  return true;
}
void saveSettings() {
  // EEPROM writes can block briefly: disconnect before writing.
  setOutput(false);
  state = lockReason == UNLOCKED ? WAITING : LOCKED;
  qualifying = false;
  uint8_t bytes[12] = {'V', 'P', 1,
    (uint8_t)settings.vmin, (uint8_t)(settings.vmin >> 8),
    (uint8_t)settings.vmax, (uint8_t)(settings.vmax >> 8),
    (uint8_t)settings.imax, (uint8_t)(settings.imax >> 8), settings.delaySeconds, 0, 0};
  uint16_t crc = checksum(bytes, 10);
  bytes[10] = (uint8_t)crc; bytes[11] = (uint8_t)(crc >> 8);
  for (uint8_t i = 0; i < 12; i++) EEPROM.update(i, bytes[i]);
  recordEvent(SAVE_EVENT, faultMask, (uint32_t)millis());
  queueFlash(F("SAVED; load waits for healthy readings\n"));
}

void recordEvent(EventKind kind, uint8_t cause, uint32_t now) {
  events[eventHead] = {now, voltage, current, settings.vmin, settings.vmax, settings.imax,
                       (uint8_t)kind, cause, recentTrips};
  eventHead = (uint8_t)((eventHead + 1) % 8);
  if (eventCount < 8) eventCount++;
}
const char *eventName(uint8_t kind) {
  switch (kind) {
    case BOOT_EVENT: return "BOOT";
    case ON_EVENT: return "ON";
    case TRIP_EVENT: return "TRIP";
    case REPEAT_LOCK_EVENT: return "LOCK_REPEAT";
    case RELAY_LOCK_EVENT: return "LOCK_RELAY";
    case ACK_EVENT: return "ACK";
    case DENIED_EVENT: return "ACK_DENIED";
    case SET_EVENT: return "SETTING";
    default: return "SAVE";
  }
}
void startLog() {
  logCount = eventCount;
  logIndex = 0;
  uint8_t oldest = (uint8_t)((eventHead + 8 - eventCount) % 8);
  for (uint8_t i = 0; i < logCount; i++) logSnapshot[i] = events[(oldest + i) % 8];
  queueFlash(F("LOG begin; t in ms, V x10, I and IMAX in mA; causes in readme\n"));
}
void streamLog() {
  if (logIndex >= logCount) return;
  const Event &e = logSnapshot[logIndex];
  char line[128];
  int n = snprintf(line, sizeof(line),
    "LOG t=%lu %s cause=%u V10=%u I=%u VMIN10=%u VMAX10=%u IMAX=%u trips=%u\n",
    (unsigned long)e.at, eventName(e.kind), e.cause, e.v, e.i, e.vmin, e.vmax, e.imax, e.trips);
  // Reserve room for the final marker as well as the event line.
  if (n > 0 && n < (int)sizeof(line) && txFree() >= (uint8_t)(n + 8)) {
    queueText(line);
    logIndex++;
    if (logIndex == logCount) queueFlash(F("LOG end\n"));
  }
}
uint8_t gridFault() {
  uint8_t f = 0;
  if (voltage < settings.vmin) f |= UNDER_VOLTAGE;
  if (voltage > settings.vmax) f |= OVER_VOLTAGE;
  if (current > settings.imax) f |= OVER_CURRENT;
  return f;
}
bool recoverySafe() {
  return voltage >= settings.vmin + VOLTAGE_MARGIN &&
         voltage <= settings.vmax - VOLTAGE_MARGIN &&
         current <= settings.imax - CURRENT_MARGIN;
}
uint32_t recoveryDelayMs() {
  uint32_t base = (uint32_t)settings.delaySeconds * 1000UL;
  return base > tripBackoffMs ? base : tripBackoffMs;
}
void expireTrips(uint32_t now) {
  while (recentTrips && (uint32_t)(now - tripTimes[0]) >= TRIP_WINDOW_MS) {
    for (uint8_t i = 1; i < recentTrips; i++) tripTimes[i - 1] = tripTimes[i];
    recentTrips--;
  }
}
void enterLock(LockReason reason, uint8_t cause, uint32_t now) {
  setOutput(false);
  qualifying = false;
  state = LOCKED;
  if (lockReason != UNLOCKED) return;
  lockReason = reason;
  // Lockouts survive a reset when EEPROM is retained; log history remains in RAM.
  EEPROM.update(LOCK_EEPROM_ADDRESS, (uint8_t)reason);
  recordEvent(reason == REPEATED_TRIPS ? REPEAT_LOCK_EVENT : RELAY_LOCK_EVENT, cause, now);
  queueFlash(F("LOCKOUT: correct fault, then press ACK or send ACK\n"));
}
void acknowledge() {
  if (lockReason == UNLOCKED) { queueFlash(F("ACK: no lockout to clear\n")); return; }
  // No immediate reconnect. ACK only returns to the ordinary qualification timer.
  if (!recoverySafe() || digitalRead(FEEDBACK_PIN) == HIGH ||
      (uint32_t)((uint32_t)millis() - outputChangedAt) < RELAY_SETTLE_MS) {
    uint8_t cause = digitalRead(FEEDBACK_PIN) == HIGH ? (uint8_t)RELAY_STUCK_CLOSED : gridFault();
    if (cause == 0) cause = 32; // Recovery margin or relay-settle permission not met.
    recordEvent(DENIED_EVENT, cause, (uint32_t)millis());
    queueFlash(F("ACK DENIED: restore safe inputs and open main contact first\n"));
    return;
  }
  setOutput(false);
  EEPROM.update(LOCK_EEPROM_ADDRESS, 0);
  lockReason = UNLOCKED;
  recentTrips = 0; tripBackoffMs = 0; mismatchActive = false;
  qualifying = false; faultMask = 0; state = WAITING; menuPage = 0;
  recordEvent(ACK_EVENT, 0, (uint32_t)millis());
  queueFlash(F("ACK accepted; fresh recovery delay starts\n"));
}
void checkRelayFeedback(uint32_t now) {
  if (lockReason != UNLOCKED) return;
  if ((uint32_t)(now - outputChangedAt) < RELAY_SETTLE_MS) { mismatchActive = false; return; }
  bool closed = digitalRead(FEEDBACK_PIN) == HIGH;
  if (closed == outputEnabled) { mismatchActive = false; return; }
  if (!mismatchActive) { mismatchActive = true; mismatchSince = now; }
  else if ((uint32_t)(now - mismatchSince) >= RELAY_MISMATCH_MS) {
    faultMask |= closed ? RELAY_STUCK_CLOSED : RELAY_STUCK_OPEN;
    enterLock(closed ? CONTACT_CLOSED : CONTACT_OPEN,
              closed ? RELAY_STUCK_CLOSED : RELAY_STUCK_OPEN, now);
  }
}
void protect(uint32_t now) {
  State previous = state;
  uint8_t previousFault = faultMask;
  expireTrips(now);
  faultMask = gridFault();
  if (lockReason == CONTACT_CLOSED) faultMask |= RELAY_STUCK_CLOSED;
  if (lockReason == CONTACT_OPEN) faultMask |= RELAY_STUCK_OPEN;
  if (lockReason != UNLOCKED) {
    setOutput(false); qualifying = false; state = LOCKED;
  } else if (faultMask != NO_FAULT) {
    bool wasConnected = outputEnabled;
    setOutput(false);
    qualifying = false;
    state = TRIPPED;
    if (wasConnected) {
      // Count disconnected loads, not samples of one sustained fault.
      if (recentTrips < 3) tripTimes[recentTrips++] = now;
      tripBackoffMs = recentTrips >= 2 ? SECOND_TRIP_DELAY_MS : 0;
      recordEvent(TRIP_EVENT, faultMask, now);
      if (recentTrips >= 3) enterLock(REPEATED_TRIPS, faultMask, now);
    }
  } else if (outputEnabled) {
    state = CONNECTED;
    if (recentTrips == 0) tripBackoffMs = 0;
  } else {
    // Hysteresis is applied on recovery, not on the initial trip.
    state = WAITING;
    if (!recoverySafe()) qualifying = false;
    else {
      if (!qualifying) { qualifying = true; healthySince = now; }
      if ((uint32_t)(now - healthySince) >= recoveryDelayMs()) {
        setOutput(true);
        state = CONNECTED;
        recordEvent(ON_EVENT, 0, now);
      }
    }
  }
  checkRelayFeedback(now);
  digitalWrite(FAULT_LED_PIN, faultMask || lockReason != UNLOCKED ? HIGH : LOW);
  if (previous != state || previousFault != faultMask) queueStatus();
}
void sampleAndProtect(uint32_t now) {
  voltage = (uint16_t)((uint32_t)analogRead(VOLTAGE_PIN) * 3000UL / 1023UL);
  current = (uint16_t)((uint32_t)analogRead(CURRENT_PIN) * 10000UL / 1023UL);
  protect(now);
}
// Strict decimal parser: rejects NaN, negative values, trailing junk and overflow.
bool parseScaled(const char *text, uint8_t decimals, uint32_t &value) {
  if (!text || !*text) return false;
  uint32_t whole = 0, fraction = 0;
  uint8_t wholeDigits = 0, fractionalDigits = 0;
  while (*text >= '0' && *text <= '9') {
    if (++wholeDigits > 5) return false;
    whole = whole * 10 + (*text++ - '0');
  }
  if (!wholeDigits) return false;
  if (*text == '.') {
    text++;
    while (*text >= '0' && *text <= '9') {
      if (++fractionalDigits > decimals) return false;
      fraction = fraction * 10 + (*text++ - '0');
    }
    if (!fractionalDigits) return false;
  }
  if (*text) return false;
  uint32_t factor = 1;
  for (uint8_t i = 0; i < decimals; i++) factor *= 10;
  for (uint8_t i = fractionalDigits; i < decimals; i++) fraction *= 10;
  value = whole * factor + fraction;
  return true;
}
void settingsChanged(uint32_t now) {
  qualifying = false; // A new setting starts a fresh recovery interval.
  protect(now);      // A tighter threshold may disconnect immediately.
  recordEvent(SET_EVENT, faultMask, now);
  queueSettings();
}
void handleCommand(char *line) {
  for (char *p = line; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
  char *command = strtok(line, " \t");
  if (!command) return;
  char *field = strtok(NULL, " \t");
  if (strcmp(command, "SET") != 0) {
    if (field) { queueFlash(F("ERR unexpected argument\n")); return; }
    if (strcmp(command, "STATUS") == 0) { queueStatus(); queueSettings(); }
    else if (strcmp(command, "LOG") == 0) startLog();
    else if (strcmp(command, "ACK") == 0) acknowledge();
    else if (strcmp(command, "SAVE") == 0) saveSettings();
    else if (strcmp(command, "DEFAULTS") == 0) {
      settings = {1800, 2500, 5000, 3};
      settingsChanged((uint32_t)millis());
      queueFlash(F("DEFAULTS in RAM; SAVE to persist\n"));
    } else if (strcmp(command, "HELP") == 0) {
      queueFlash(F("SET VMIN <V> | SET VMAX <V> | SET IMAX <A> | SET DELAY <seconds>\nSTATUS | LOG | ACK | SAVE | DEFAULTS | HELP\n"));
    } else queueFlash(F("ERR unknown command; use HELP\n"));
    return;
  }
  char *argument = strtok(NULL, " \t");
  char *extra = strtok(NULL, " \t");
  if (!field || !argument || extra) { queueFlash(F("ERR use SET field value\n")); return; }
  uint8_t decimals;
  if (strcmp(field, "VMIN") == 0 || strcmp(field, "VMAX") == 0) decimals = 1;
  else if (strcmp(field, "IMAX") == 0) decimals = 3;
  else if (strcmp(field, "DELAY") == 0) decimals = 0;
  else { queueFlash(F("ERR unknown setting\n")); return; }
  uint32_t value;
  if (!parseScaled(argument, decimals, value) || value > 65535UL ||
      (strcmp(field, "DELAY") == 0 && value > 255UL)) {
    queueFlash(F("ERR invalid number\n")); return;
  }
  Settings candidate = settings;
  if (strcmp(field, "VMIN") == 0) candidate.vmin = (uint16_t)value;
  else if (strcmp(field, "VMAX") == 0) candidate.vmax = (uint16_t)value;
  else if (strcmp(field, "IMAX") == 0) candidate.imax = (uint16_t)value;
  else candidate.delaySeconds = (uint8_t)value;
  if (!validSettings(candidate)) { queueFlash(F("ERR range or VMIN/VMAX gap; see readme\n")); return; }
  settings = candidate;
  settingsChanged((uint32_t)millis());
}
void pollSerial() {
  uint8_t budget = 16;
  while (budget-- && Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (rxOverflow) queueFlash(F("ERR line too long; ignored\n"));
      else { rxLine[rxLength] = '\0'; handleCommand(rxLine); }
      rxLength = 0; rxOverflow = false;
    } else if (!rxOverflow) {
      if (rxLength < sizeof(rxLine) - 1) rxLine[rxLength++] = c;
      else rxOverflow = true;
    }
  }
}
bool pressed(Button &button, uint32_t now) {
  uint8_t reading = (uint8_t)digitalRead(button.pin);
  if (reading != button.raw) { button.raw = reading; button.changedAt = now; }
  if (reading != button.stable && (uint32_t)(now - button.changedAt) >= 25) {
    button.stable = reading;
    return reading == LOW;
  }
  return false;
}
void adjustSetting(int8_t direction, uint32_t now) {
  Settings candidate = settings;
  if (menuPage == 1) candidate.vmax = (uint16_t)((int32_t)candidate.vmax + direction * 50);
  else if (menuPage == 2) candidate.imax = (uint16_t)((int32_t)candidate.imax + direction * 100);
  else if (menuPage == 3) candidate.delaySeconds = (uint8_t)((int16_t)candidate.delaySeconds + direction);
  else if (menuPage == 4) candidate.vmin = (uint16_t)((int32_t)candidate.vmin + direction * 50);
  else return;
  if (!validSettings(candidate)) { queueFlash(F("ERR setting limit\n")); return; }
  settings = candidate;
  settingsChanged(now);
}
void pollButtons(uint32_t now) {
  if (pressed(ackButton, now)) acknowledge();
  if (pressed(setButton, now) && lockReason == UNLOCKED) {
    menuPage = (uint8_t)((menuPage + 1) % 5);
    if (menuPage == 0) saveSettings();
  }
  if (pressed(upButton, now) && lockReason == UNLOCKED) adjustSetting(1, now);
  if (pressed(downButton, now) && lockReason == UNLOCKED) adjustSetting(-1, now);
}
void lcdLine(uint8_t row, const char *text) {
  lcd.setCursor(0, row);
  uint8_t i = 0;
  while (*text && i < 16) { lcd.write((uint8_t)*text++); i++; }
  while (i++ < 16) lcd.write((uint8_t)' ');
}
void updateDisplay() {
  char line[24];
  if (lockReason != UNLOCKED) {
    lcdLine(0, lockReason == REPEATED_TRIPS ? "LOCK: 3 TRIPS" : "LOCK: RELAY FAIL");
    lcdLine(1, "FIX; PRESS ACK");
    return;
  }
  if (menuPage == 0) {
    snprintf(line, sizeof(line), "V:%u.%u I:%u.%02u", voltage / 10, voltage % 10,
             current / 1000, (current % 1000) / 10);
    lcdLine(0, line);
    if (state == CONNECTED) lcdLine(1, "ON - LOAD ACTIVE");
    else if (state == WAITING) {
      snprintf(line, sizeof(line), "WAIT %lu s OFF", (unsigned long)(recoveryDelayMs() / 1000UL));
      lcdLine(1, line);
    }
    else if (faultMask & OVER_CURRENT) lcdLine(1, "OVERCURRENT OFF");
    else if (faultMask & OVER_VOLTAGE) lcdLine(1, "OVERVOLTAGE OFF");
    else lcdLine(1, "UNDERVOLTAGE OFF");
  } else {
    if (menuPage == 1) snprintf(line, sizeof(line), "VMAX: %u.%u V", settings.vmax / 10, settings.vmax % 10);
    else if (menuPage == 2) snprintf(line, sizeof(line), "IMAX: %u.%02u A", settings.imax / 1000, (settings.imax % 1000) / 10);
    else if (menuPage == 3) snprintf(line, sizeof(line), "DELAY: %u s", settings.delaySeconds);
    else snprintf(line, sizeof(line), "VMIN: %u.%u V", settings.vmin / 10, settings.vmin % 10);
    lcdLine(0, line);
    snprintf(line, sizeof(line), "%s SET next", stateName());
    lcdLine(1, line);
  }
}
void setup() {
  // Preload OFF before making the relay pins outputs.
  digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
  digitalWrite(BACKUP_PIN, RELAY_OFF_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BACKUP_PIN, OUTPUT);
  setOutput(false);
  pinMode(FEEDBACK_PIN, INPUT); // 10k external pulldown at the main NO node.
  pinMode(FAULT_LED_PIN, OUTPUT);
  pinMode(SET_PIN, INPUT_PULLUP);
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  pinMode(ACK_PIN, INPUT_PULLUP);
  Serial.begin(SERIAL_BAUD);
  bool restored = loadSettings();
  uint8_t storedLock = EEPROM.read(LOCK_EEPROM_ADDRESS);
  if (storedLock != 0 && storedLock != 255)
    lockReason = storedLock <= 3 ? (LockReason)storedLock : LOCK_RECORD_INVALID;
  lcd.begin(16, 2);
  queueFlash(F("Voltage Protector Plus - 5 V simulation; LOG and ACK available\n"));
  queueFlash(restored ? F("EEPROM settings restored\n") : F("Defaults loaded\n"));
  queueSettings();
  sampleAndProtect((uint32_t)millis());
  recordEvent(BOOT_EVENT, faultMask, (uint32_t)millis());
  updateDisplay();
  lastSample = lastDisplay = lastTelemetry = (uint32_t)millis();
}
void loop() {
  uint32_t now = (uint32_t)millis();
  if ((uint32_t)(now - lastSample) >= SAMPLE_MS) {
    lastSample = now;
    sampleAndProtect(now);
  }
  pollSerial();
  pollButtons((uint32_t)millis());
  now = (uint32_t)millis();
  if ((uint32_t)(now - lastDisplay) >= DISPLAY_MS) {
    lastDisplay = now;
    updateDisplay();
  }
  if ((uint32_t)(now - lastTelemetry) >= TELEMETRY_MS) {
    lastTelemetry = now;
    if (logIndex >= logCount) queueStatus();
  }
  streamLog();
  flushSerial();
}
