// Arduino Uno - Throttle + Gear Sender for Speedometer Web UI
// Reads an analog input (e.g. potentiometer) on A0 for throttle,
// and two digital pins for the handlebar gear rocker (I / II / III),
// then sends both over serial in one line the web UI understands:
//   ADC=<0..4095>,GEAR=<1|2|3>
//
// Throttle wiring (if using a potentiometer as throttle):
//   Pot pin 1 -> 5V
//   Pot pin 2 (wiper) -> A0
//   Pot pin 3 -> GND
//
// Gear rocker wiring (per the throttle housing, confirmed by multimeter):
//   Brown wire (centre, common) -> 5V
//   White wire                  -> D2 (GEAR_LEFT_PIN), + 5k ohm resistor D2 to GND
//   Blue wire                   -> D3 (GEAR_RIGHT_PIN), + 5k ohm resistor D3 to GND
// (The pull-down resistors are required because the centre wire is 5V, not
//  GND, so the signal pins would otherwise float when the switch isn't
//  pressing them high. Uno has no built-in pull-down, only pull-up.)
// Note: "LEFT"/"RIGHT" here are just pin labels carried over from testing —
// they don't necessarily match the physical left/right of the switch body.
// What matters is D2=white, D3=blue, matching the truth table below.
//
// Truth table (confirmed with multimeter + Serial Monitor testing):
//   LEFT=0, RIGHT=0 -> Gear II  (Normal)
//   LEFT=1, RIGHT=0 -> Gear III (Sport)
//   LEFT=0, RIGHT=1 -> Gear I   (Eco)
//
// F/R (forward/reverse) rocker wiring — SEPARATE connector, only 2 wires
// since it's a 2-state switch (unlike the 3-wire gear rocker):
//   Purple wire -> 5V (common)
//   Yellow wire -> D4 (DIR_PIN), + 5k ohm resistor D4 to GND
//
// ASSUMED (not yet multimeter-confirmed — check with Serial Monitor the
// same way the gear table above was confirmed, flip DIR_REVERSED_WHEN_HIGH
// below if F and R come out backwards):
//   DIR_PIN = LOW  -> Forward
//   DIR_PIN = HIGH -> Reverse
//
// --- Cruise control (v5) ---
// CRUISE_BUTTON_PIN and BRAKE_PIN are wired the same active-HIGH way as
// the gear/DIR switches: one leg to 5V, the other leg to the Arduino pin
// AND to GND through a resistor (pull-down). Idle reads LOW, pressed
// reads HIGH — confirmed on the bench (D5 idle=0/pressed=1, D6
// idle=0/pressed=1). No internal pull-up is used here since the
// external resistor already sets the idle state.
//
// Toggles cruise: press once to engage, press again to cancel.
//
// BRAKE_PIN: pressed = HIGH cancels cruise immediately, at any time.
//
// Third cancel condition: once cruise is engaged, if the live throttle
// has fully returned to rest (idle) at some point, and *after* that a
// sudden jump of >= CRUISE_SPIKE_ADC within CRUISE_SPIKE_WINDOW_MS
// happens, cruise cancels. This is meant to catch "grabbing the throttle
// again after resting your hand" while cruising. It intentionally does
// NOT trigger just because you eased off the throttle to rest — only a
// rest THEN a spike disengages it.
//
// While cruise is active, the ADC value sent over serial is the LATCHED
// value from the moment cruise engaged (so the web gauge holds steady),
// not the live throttle reading. The live reading keeps being sampled
// in the background purely to watch for the rest+spike cancel condition.
//
// Must match the baud rate used in script.js (115200).
//
// --- Adaptive cruise control / gap sensor (v6) ---
// Adds a Waveshare TOF Laser Range Sensor (D) read over a SECOND, software
// UART (hardware Serial is already busy talking to the web UI), plus a new
// ACC toggle button. ACC is a SEPARATE mode from the existing flat-speed
// cruise above — only one of the two can be active at a time (engaging one
// cancels the other), so there's never two conflicting speed targets.
//
// This sketch's only job for ACC is: read the sensor, forward the raw
// distance and the ACC on/off flag over serial. All the actual gap-control
// math (time-gap target, PD control, lost-target fallback, output
// smoothing) lives in script.js, driving the simulated vehicle speed. No
// physical throttle actuation happens here.
//
// TOF sensor wiring (UART mode, NLink_TOFSense_Frame0 protocol):
//   Sensor TX -> D7 (TOF_RX_PIN)
//   Sensor RX -> D8 (TOF_TX_PIN)  [unused for reading, SoftwareSerial needs it declared anyway]
//   Sensor VCC/GND per its datasheet.
//
// IMPORTANT: this sensor defaults to 921600 baud, which SoftwareSerial on
// an Uno cannot reliably sustain. Before wiring it in, connect it to a
// USB-TTL adapter, open Waveshare's TOF Assistant tool, and change the
// baud rate to 115200 (TOF_BAUD_RATE below) — otherwise frames will be
// garbled/dropped.
//
// Frame format (confirmed against Nooploop's NLink_TOFSense_Frame0 spec):
// 16 bytes total, little-endian.
//   byte 0     : header, fixed 0x57
//   byte 1     : function mark, fixed 0x00 for output frames
//   byte 3     : sensor ID
//   bytes 4-7  : system_time (uint32, unused here)
//   bytes 8-10 : distance, int24 LE, sign-extend to int32, value is mm
//   byte 11    : dis_status (0 = normal/trustworthy on the bench; anything
//                else is treated as an unreliable reading here — flip this
//                assumption if TOF Assistant shows different status codes
//                for your unit)
//   bytes 12-13: signal_strength (uint16, unused here)
//   byte 15    : checksum = low byte of sum(bytes 0..14)
//
// ACC_BUTTON_PIN wired identically to CRUISE_BUTTON_PIN/BRAKE_PIN: one leg
// to 5V, the other leg to the pin + external 5k pull-down to GND. Toggle:
// press once to engage ACC, press again to cancel. Brake cancels it too.

#include <SoftwareSerial.h>

const int THROTTLE_PIN      = A0;  // analog input pin
const int GEAR_LEFT_PIN     = 2;   // digital input, needs 5k pull-down to GND
const int GEAR_RIGHT_PIN    = 3;   // digital input, needs 5k pull-down to GND
const int DIR_PIN           = 4;   // digital input, needs 5k pull-down to GND
const int CRUISE_BUTTON_PIN = 5;   // digital input, external 5k pull-down, HIGH = pressed
const int BRAKE_PIN         = 6;   // digital input, external 5k pull-down, HIGH = pressed (placeholder button for now)
const int TOF_RX_PIN        = 7;   // SoftwareSerial RX, wire to TOF sensor TX
const int TOF_TX_PIN        = 8;   // SoftwareSerial TX, wire to TOF sensor RX (unused, but pin must exist)
const int ACC_BUTTON_PIN    = 9;   // digital input, external 5k pull-down, HIGH = pressed

// Flip this to false if testing shows HIGH = Forward instead of Reverse.
const bool DIR_REVERSED_WHEN_HIGH = true;

const unsigned long BAUD_RATE = 115200;
const unsigned long SEND_INTERVAL_MS = 30; // ~33 updates/sec, smooth enough for the gauge

// Throttle is considered "at rest" at/below this scaled ADC value. Set
// just above the 1120 idle baseline script.js already uses, so normal
// sensor noise at idle doesn't flicker in and out of "rest".
const int THROTTLE_REST_ADC = 1200;

// 30% of the full 0..4095 ADC range — the spike-detection threshold.
const int CRUISE_SPIKE_ADC = (int)(4095 * 0.30);

// Window to measure the spike over.
const unsigned long CRUISE_SPIKE_WINDOW_MS = 1000;

// History buffer of live scaled-ADC samples, one slot per send cycle,
// long enough to cover the spike window (used to compare "now" against
// "~1 second ago" without needing a timestamp per sample).
const int HISTORY_LEN = (CRUISE_SPIKE_WINDOW_MS / SEND_INTERVAL_MS) + 2;
int throttleHistory[HISTORY_LEN];
int historyIndex = 0;
bool historyFull = false;

bool cruiseActive = false;
int cruiseLatchedAdc = 0;
bool cruiseReachedRest = false; // has live throttle hit rest since cruise engaged?

bool lastButtonPressed = false; // for edge detection on the toggle button

unsigned long lastSendTime = 0;

// --- TOF sensor (adaptive cruise gap input) ---
SoftwareSerial tofSerial(TOF_RX_PIN, TOF_TX_PIN);

// Reconfigure the sensor to this via TOF Assistant before wiring it in —
// see the header comment above. Do NOT leave it at the 921600 default.
const unsigned long TOF_BAUD_RATE = 115200;

const int TOF_FRAME_LEN = 16;
uint8_t tofFrame[TOF_FRAME_LEN];
int tofFrameIndex = 0;

long tofDistanceMm = -1;          // -1 = no valid/trustworthy reading yet
unsigned long tofLastValidMs = 0; // millis() of the last accepted frame

// If no valid frame arrives within this window, treat the target as lost
// (sensor out of range past 40m, nothing in view, or a run of bad frames).
const unsigned long TOF_TIMEOUT_MS = 300;

// --- Adaptive cruise control (ACC) ---
bool accActive = false;
bool lastAccButtonPressed = false; // for edge detection on the ACC toggle button

// Debounced digital read: takes two samples 2ms apart and only trusts the
// result if they agree. Retries a few times if the pin is bouncing/noisy,
// then just returns the latest sample rather than retrying forever (an
// unbounded retry loop could hang the loop() if a pin stays noisy).
int readDebounced(int pin) {
  const int MAX_ATTEMPTS = 10;

  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    int a = digitalRead(pin);
    delay(2);
    int b = digitalRead(pin);
    if (a == b) return a;
  }

  return digitalRead(pin); // give up trying to settle, just use the latest read
}

int readGear() {
  bool left  = readDebounced(GEAR_LEFT_PIN)  == HIGH;
  bool right = readDebounced(GEAR_RIGHT_PIN) == HIGH;

  if (left && !right)  return 3; // Sport
  if (!left && right)  return 1; // Eco
  return 2;                      // Normal (both low, or the rare both-high glitch)
}

// Returns 1 for Forward, 2 for Reverse.
int readDirection() {
  bool high = readDebounced(DIR_PIN) == HIGH;
  bool reverse = DIR_REVERSED_WHEN_HIGH ? high : !high;
  return reverse ? 2 : 1;
}

// Pushes a new live throttle sample into the ring buffer.
void pushHistory(int scaledAdc) {
  throttleHistory[historyIndex] = scaledAdc;
  historyIndex = (historyIndex + 1) % HISTORY_LEN;
  if (historyIndex == 0) historyFull = true;
}

// The oldest sample currently in the buffer — approximately
// CRUISE_SPIKE_WINDOW_MS old, since we push one sample per send cycle.
int oldestHistorySample() {
  // historyIndex points at the slot that will be overwritten next,
  // which is exactly the oldest sample once the buffer has wrapped.
  return throttleHistory[historyIndex];
}

// True if the live throttle has jumped by >= CRUISE_SPIKE_ADC compared
// to ~CRUISE_SPIKE_WINDOW_MS ago.
bool detectedThrottleSpike(int liveScaledAdc) {
  if (!historyFull) return false; // not enough history yet, don't false-trigger at boot/right after engaging
  int past = oldestHistorySample();
  return (liveScaledAdc - past) >= CRUISE_SPIKE_ADC;
}

// Reads the cruise toggle button and returns true exactly once per
// press (rising edge on "pressed"), so holding it down doesn't spam
// toggles.
bool cruiseButtonPressedEdge() {
  bool pressed = readDebounced(CRUISE_BUTTON_PIN) == HIGH;
  bool edge = pressed && !lastButtonPressed;
  lastButtonPressed = pressed;
  return edge;
}

bool brakePressed() {
  return readDebounced(BRAKE_PIN) == HIGH;
}

// Runs the whole cruise state machine for one cycle. liveScaledAdc is
// the real-time throttle reading (0..4095) for this cycle; returns the
// ADC value that should actually be sent over serial (latched if
// cruising, live otherwise).
int updateCruiseControl(int liveScaledAdc) {
  // 1. Button toggles cruise on/off.
  if (cruiseButtonPressedEdge()) {
    if (cruiseActive) {
      cruiseActive = false;
    } else {
      cruiseActive = true;
      cruiseLatchedAdc = liveScaledAdc;
      cruiseReachedRest = false;
      accActive = false; // mutually exclusive with ACC
    }
  }

  // 2. Brake cancels cruise immediately, any time.
  if (cruiseActive && brakePressed()) {
    cruiseActive = false;
  }

  // 3. Rest-then-spike cancels cruise.
  if (cruiseActive) {
    if (!cruiseReachedRest && liveScaledAdc <= THROTTLE_REST_ADC) {
      cruiseReachedRest = true;
    }
    if (cruiseReachedRest && detectedThrottleSpike(liveScaledAdc)) {
      cruiseActive = false;
    }
  }

  if (!cruiseActive) {
    cruiseReachedRest = false;
  }

  return cruiseActive ? cruiseLatchedAdc : liveScaledAdc;
}

// Pulls in whatever bytes are waiting on the TOF's software UART, hunts
// for a valid 16-byte frame (header 0x57, function mark 0x00, checksum
// verified), and updates tofDistanceMm when one is found. Re-syncs on any
// mismatch instead of getting stuck, so a single dropped/garbled byte
// can't wedge the parser. Call this every loop() pass (not gated by
// SEND_INTERVAL_MS) so the software UART's small internal buffer doesn't
// overflow between reads.
void readTofSensor() {
  while (tofSerial.available()) {
    uint8_t b = tofSerial.read();

    if (tofFrameIndex == 0) {
      if (b != 0x57) continue; // not a header byte, keep hunting
    } else if (tofFrameIndex == 1) {
      if (b != 0x00) { tofFrameIndex = 0; continue; } // wrong function mark, resync
    }

    tofFrame[tofFrameIndex++] = b;

    if (tofFrameIndex >= TOF_FRAME_LEN) {
      tofFrameIndex = 0; // always reset for the next frame, valid or not

      uint8_t checksum = 0;
      for (int i = 0; i < TOF_FRAME_LEN - 1; i++) checksum += tofFrame[i];

      if (checksum == tofFrame[TOF_FRAME_LEN - 1]) {
        uint8_t status = tofFrame[11];

        // Distance is a little-endian int24 spread across bytes 8-10.
        // Shift into the top of an int32 then sign-extend from bit 23.
        int32_t raw24 = (int32_t)tofFrame[8] | ((int32_t)tofFrame[9] << 8) | ((int32_t)tofFrame[10] << 16);
        if (raw24 & 0x00800000) raw24 |= 0xFF000000;

        if (status == 0) { // treat non-zero status as an unreliable reading
          tofDistanceMm = raw24;
          tofLastValidMs = millis();
        }
      }
      // Bad checksum -> just drop this frame; the next 0x57 starts fresh.
    }
  }

  if (millis() - tofLastValidMs > TOF_TIMEOUT_MS) {
    tofDistanceMm = -1; // nothing recent enough to trust -> lost target
  }
}

bool accButtonPressedEdge() {
  bool pressed = readDebounced(ACC_BUTTON_PIN) == HIGH;
  bool edge = pressed && !lastAccButtonPressed;
  lastAccButtonPressed = pressed;
  return edge;
}

// ACC and flat-speed cruise are mutually exclusive — only one auto-speed
// mode runs at a time, so there's never two conflicting targets. Engaging
// one cancels the other (the cruise side of this is handled inside
// updateCruiseControl()). Brake cancels ACC immediately, same as cruise.
void updateAccControl() {
  if (accButtonPressedEdge()) {
    accActive = !accActive;
    if (accActive) cruiseActive = false;
  }

  if (accActive && brakePressed()) {
    accActive = false;
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(THROTTLE_PIN, INPUT);
  pinMode(GEAR_LEFT_PIN, INPUT);
  pinMode(GEAR_RIGHT_PIN, INPUT);
  pinMode(DIR_PIN, INPUT);
  pinMode(CRUISE_BUTTON_PIN, INPUT);
  pinMode(BRAKE_PIN, INPUT);
  pinMode(ACC_BUTTON_PIN, INPUT);

  tofSerial.begin(TOF_BAUD_RATE);

  for (int i = 0; i < HISTORY_LEN; i++) throttleHistory[i] = 0;
}

void loop() {
  // Not gated by SEND_INTERVAL_MS — needs to run every pass so the
  // software UART's small buffer doesn't overflow between sends.
  readTofSensor();
  updateAccControl();

  unsigned long now = millis();

  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;

    int raw = analogRead(THROTTLE_PIN); // 0..1023 on Uno

    // Web UI expects a 0..4095 style ADC range (STM32 12-bit ADC).
    // Uno's ADC is 10-bit (0..1023), so scale it up to keep the
    // same ADC=1120..4095 mapping used in script.js.
    int liveScaled = map(raw, 0, 1023, 0, 4095);
    int gear = readGear();
    int dir = readDirection();

    // Cruise control runs on the live value, and hands back whichever
    // value should actually be reported (latched or live). The live
    // sample also gets pushed into history for spike detection,
    // regardless of whether cruise is currently active.
    int reportedScaled = updateCruiseControl(liveScaled);
    pushHistory(liveScaled);

    Serial.print("ADC=");
    Serial.print(reportedScaled);
    Serial.print(",GEAR=");
    Serial.print(gear);
    Serial.print(",DIR=");
    Serial.print(dir);
    Serial.print(",CRUISE=");
    Serial.print(cruiseActive ? 1 : 0);
    Serial.print(",ACC=");
    Serial.print(accActive ? 1 : 0);
    Serial.print(",DIST=");
    Serial.println(tofDistanceMm); // mm, -1 = no valid target
  }
}