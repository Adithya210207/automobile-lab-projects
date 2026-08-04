// Arduino Uno - Throttle + Gear + Cruise Sender for Speedometer Web UI
// Reads an analog input (e.g. potentiometer) on A0 for throttle,
// two digital pins for the handlebar gear rocker (I / II / III),
// and one digital pin for the F/R cruise-control rocker,
// then sends it all over serial in one line the web UI understands:
//   ADC=<0..4095>,GEAR=<1|2|3>,CC=<0|1>
//
// Throttle wiring (if using a potentiometer as throttle):
//   Pot pin 1 -> 5V
//   Pot pin 2 (wiper) -> A0
//   Pot pin 3 -> GND
//
// Gear rocker wiring (per the throttle housing, confirmed by multimeter):
//   Brown wire (centre, common) -> 5V
//   White wire                  -> D2 (GEAR_LEFT_PIN), + 10k ohm resistor D2 to GND
//   Blue wire                   -> D3 (GEAR_RIGHT_PIN), + 10k ohm resistor D3 to GND
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
// F/R rocker wiring (separate 2-wire connector, blue + yellow):
// Only two wires means only one bit of info, so it's wired like a single
// channel of the gear switch: one wire is the 5V common, the other is the
// signal wire into FR_PIN, with a 10k pull-down resistor from FR_PIN to GND
// (same reasoning as the gear switch above). Which physical wire is which
// hasn't been confirmed with a multimeter yet -- if F and R come out
// backwards once you test it on the Serial Monitor, just flip the
// CRUISE_ACTIVE_WHEN_HIGH constant below rather than re-wiring anything.
//
// Must match the baud rate used in script.js (115200).

const int THROTTLE_PIN   = A0;  // analog input pin
const int GEAR_LEFT_PIN  = 2;   // digital input, needs 10k pull-down to GND
const int GEAR_RIGHT_PIN = 3;   // digital input, needs 10k pull-down to GND
const int FR_PIN         = 4;   // digital input, needs 10k pull-down to GND (F/R cruise rocker)

// Flip this if testing shows the switch reads backwards (R reading LOW
// instead of HIGH, or vice versa) -- no rewiring needed, just flip the bool.
const bool CRUISE_ACTIVE_WHEN_HIGH = true;

const unsigned long BAUD_RATE = 115200;
const unsigned long SEND_INTERVAL_MS = 30; // ~33 updates/sec, smooth enough for the gauge

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

// F/R rocker: F = normal throttle control, R = cruise control.
// Returns true when cruise control is engaged (rocker in R position).
bool readCruiseEngaged() {
  bool pinHigh = readDebounced(FR_PIN) == HIGH;
  return CRUISE_ACTIVE_WHEN_HIGH ? pinHigh : !pinHigh;
}

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(THROTTLE_PIN, INPUT);
  pinMode(GEAR_LEFT_PIN, INPUT);
  pinMode(GEAR_RIGHT_PIN, INPUT);
  pinMode(FR_PIN, INPUT);
}

void loop() {
  unsigned long now = millis();

  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;

    int raw = analogRead(THROTTLE_PIN); // 0..1023 on Uno

    // Web UI expects a 0..4095 style ADC range (STM32 12-bit ADC).
    // Uno's ADC is 10-bit (0..1023), so scale it up to keep the
    // same ADC=1120..4095 mapping used in script.js.
    int scaled = map(raw, 0, 1000, 0, 4095);
    int gear = readGear();
    bool cruise = readCruiseEngaged();

    Serial.print("ADC=");
    Serial.print(scaled);
    Serial.print(",GEAR=");
    Serial.print(gear);
    Serial.print(",CC=");
    Serial.println(cruise ? 1 : 0);
  }
}