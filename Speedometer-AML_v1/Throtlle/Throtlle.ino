// Arduino Uno - Throttle Sender for Speedometer Web UI
// Reads an analog input (e.g. potentiometer) on A0 and sends it
// over serial in the format the web UI expects: ADC=<value>
//
// Wiring (if using a potentiometer as throttle):
//   Pot pin 1 -> 5V
//   Pot pin 2 (wiper) -> A0
//   Pot pin 3 -> GND
//
// Must match the baud rate used in script.js (115200).

const int THROTTLE_PIN = A0;   // analog input pin
const unsigned long BAUD_RATE = 115200;
const unsigned long SEND_INTERVAL_MS = 30; // ~33 updates/sec, smooth enough for the gauge

unsigned long lastSendTime = 0;

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(THROTTLE_PIN, INPUT);
}

void loop() {
  unsigned long now = millis();

  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;

    int raw = analogRead(THROTTLE_PIN); // 0..1023 on Uno

    // Web UI expects a 0..4095 style ADC range (STM32 12-bit ADC).
    // Uno's ADC is 10-bit (0..1023), so scale it up to keep the
    // same ADC=1120..4095 mapping used in script.js.
    int scaled = map(raw, 0, 1023, 0, 4095);

    Serial.print("ADC=");
    Serial.println(scaled);
  }
}