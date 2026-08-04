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

const int THROTTLE_PIN      = A0;  // analog input pin
const int GEAR_LEFT_PIN     = 2;   // digital input, needs 5k pull-down to GND
const int GEAR_RIGHT_PIN    = 3;   // digital input, needs 5k pull-down to GND
const int DIR_PIN           = 4;   // digital input, needs 5k pull-down to GND
const int CRUISE_BUTTON_PIN = 5;   // digital input, external 5k pull-down, HIGH = pressed
const int BRAKE_PIN         = 6;   // digital input, external 5k pull-down, HIGH = pressed (placeholder button for now)

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

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(THROTTLE_PIN, INPUT);
  pinMode(GEAR_LEFT_PIN, INPUT);
  pinMode(GEAR_RIGHT_PIN, INPUT);
  pinMode(DIR_PIN, INPUT);
  pinMode(CRUISE_BUTTON_PIN, INPUT);
  pinMode(BRAKE_PIN, INPUT);

  for (int i = 0; i < HISTORY_LEN; i++) throttleHistory[i] = 0;
}

void loop() {
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
    Serial.println(cruiseActive ? 1 : 0);
  }
}