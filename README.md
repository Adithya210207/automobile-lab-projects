# 🏎️ Automobile Lab Projects (AML) — Speedometer & Adaptive Cruise Control

An automotive laboratory project series developing an integrated digital Speedometer, Throttle Controller, and Adaptive Cruise Control (ACC) system. The project interfaces Arduino microcontroller hardware (reading throttle ADC, 3-speed gear switches, cruise/brake buttons, and TOF laser range sensors) with a dynamic Web UI dashboard via Serial communication.

---

## 📌 Project Version History & Progression

| Version | Focus / Prototype | Key Features & Implementation Details |
|---|---|---|
| 🚗 **[Speedometer-AML_v1](./Speedometer-AML_v1)** | First Prototype | • Converted potentiometer ADC values into percentages.<br>• Real-time web UI visual speedometer. |
| 🏎️ **[Speedometer-AML_v2](./Speedometer-AML_v2)** | Gear Modes | • Converted ADC values to UI percentages.<br>• Added 3 gear modes to switch speed curves. |
| ⚙️ **[Speedometer-AML_v3](./Speedometer-AML_v3)** | Cruise Control Concept | • Integrated F/R button mapped to basic cruise control toggle. |
| 🔄 **[Speedometer-AML_v4](./Speedometer-AML_v4)** | Forward / Reverse | • Dedicated F/R button functionality for Forward and Reverse drive modes. |
| 🎯 **[Speedometer-AML_v5](./Speedometer-AML_v5)** | Adaptive Cruise Control (Basic) | • Push-button cruise control engagement.<br>• Auto-cancel safety on brake press (simulated button).<br>• Auto-cancel safety on sudden 30%+ ADC throttle spike within 1 second. |
| ⚡ **[Speedometer-AML_v6](./Speedometer-AML_v6)** | Prototype 3 (Laser TOF Gap Control) | • Integrated **Waveshare TOF Laser Range Sensor** via second SoftwareSerial (D7/D8).<br>• Parsed 16-byte `NLink_TOFSense_Frame0` frame protocol in Arduino (`Throtlle.ino`).<br>• Dedicated ACC toggle on D9 (mutually exclusive with flat cruise mode).<br>• High-frequency PD gap-control algorithm in `script.js` with lost-target fallback and output smoothing. |

---

## 🛠️ System Architecture (v6 Overview)

### Hardware Interfacing (Arduino `.ino`)
- **SoftwareSerial (D7/D8)**: Receives TOF distance data from Waveshare TOF sensor.
- **Hardware Serial (USB)**: Transmits status packet strings (`ACC=0|1`, `DIST=<mm>`, `GEAR`, `THROTTLE`) to the web interface.
- **Safety Inputs**: Dedicated digital inputs for Brake interrupt and Cruise / ACC disengagement.

### Web Interface & Control Loop (`script.js` / `index.html`)
- **Web Serial API**: Direct browser-to-Arduino USB serial connection.
- **Control Loop**: Time-gap target tracking using Proportional-Derivative (PD) control.
- **Safety Rules**: Instant disengagement upon brake trigger or manual override.

---

## 📁 Repository Structure

```
Automobile Lab Projects (AML)/
├── README.md                      # Main project summary & documentation
├── .gitignore                     # Build and binary exclusions
├── Speedometer-AML_v1/            # Prototype 1 (Basic ADC Speedometer)
├── Speedometer-AML_v2/            # Prototype 2 (3-Speed Gear Switching)
├── Speedometer-AML_v3/            # Prototype 2 (Cruise Control Prototype)
├── Speedometer-AML_v4/            # Prototype 2 (Forward/Reverse Controls)
├── Speedometer-AML_v5/            # Prototype 2 (Brake Safety & Spike Auto-cancel)
└── Speedometer-AML_v6/            # Prototype 3 (Waveshare TOF Laser ACC Gap Control)
```

---

## 📄 Copyright & Terms of Use

**© 2025–2026 Adithya V. All Rights Reserved.**

This repository is published for portfolio, demonstration, and educational viewing purposes only. You are welcome to view the source code to understand the implementation.

### You may NOT:

- Copy or reproduce substantial portions of this project.
- Redistribute, republish, or mirror this repository.
- Use this code in commercial projects without prior written permission.
- Submit this code, or substantially similar code, as your own for academic purposes.
- Remove copyright notices or claim this work as your own.

If you would like to use any part of this project, please contact me first for permission.

