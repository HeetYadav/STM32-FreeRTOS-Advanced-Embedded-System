# FreeRTOS_App_L476

> **A production-grade FreeRTOS application for the STM32L476RG** — 5 concurrent tasks, hardware PWM LED bar graph, real-time ultrasonic distance sensing, IR proximity detection, and an interactive serial CLI. Designed to run on top of the [SecureBootloader_L476](../SecureBootloader_L476/).

---

## What This Does

This is the main application — the firmware that the Bootloader verifies and launches. It demonstrates multiple advanced embedded systems concepts running simultaneously in real-time:

| Capability | How It Works |
|-----------|--------------|
| **Multi-tasking** | 5 FreeRTOS tasks running concurrently, scheduled by the RTOS kernel |
| **Hardware PWM** | TIM1/TIM2/TIM3 drive LEDs directly — zero CPU usage for fading |
| **Ultrasonic ranging** | HC-SR04 measured via EXTI + TIM5 microsecond stopwatch |
| **IR detection** | HW-201 triggers on both edges — detects presence AND removal |
| **Serial CLI** | Interrupt-driven UART, type commands from any terminal |
| **Sensor gating** | Nothing runs until you type `sensor start` |

---

## Hardware Pin Map

| Signal | MCU Pin | Peripheral | Notes |
|--------|---------|------------|-------|
| LED 1 (nearest) | PB4 | TIM3_CH1 (AF2) | 220Ω series resistor |
| LED 2 | PB5 | TIM3_CH2 (AF2) | 220Ω series resistor |
| LED 3 | PB3 | TIM2_CH2 (AF1) | 220Ω series resistor |
| LED 4 (farthest) | PA8 | TIM1_CH1 (AF1) | 220Ω series resistor |
| HC-SR04 TRIG | PC7 | GPIO Output | 10µs pulse |
| HC-SR04 ECHO | PB6 | EXTI6 | RISING+FALLING interrupt |
| HW-201 IR OUT | PA7 | EXTI7 | RISING+FALLING interrupt, active LOW |
| UART TX | PA9 | USART1_TX (AF7) | 115200 baud 8N1 |
| UART RX | PA10 | USART1_RX (AF7) | Interrupt-driven |
| Blue Button | PC13 | GPIO Input | Triggers CASCADE pattern |
| Ext. Button / OTA trigger | PB10 | GPIO Input (PULLUP) | Triggers FLASH pattern; hold on reset for OTA |
| Heartbeat LED (onboard) | PA5 | GPIO Output | Blinks 1Hz to confirm RTOS running |

> 📸 **[Hardware Photo: Full pin mapping on breadboard]**
> *(Contribute a photo via PR — see [CONTRIBUTING.md](../CONTRIBUTING.md)!)*

---

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) in VS Code
- [SecureBootloader_L476](../SecureBootloader_L476/) already flashed to the board
- Python 3 (for the CRC injection post-build script)

### Build + Flash (via ST-Link)

```bash
# From the FreeRTOS_App_L476/ directory
pio run -t upload
```

The post-build script `inject_crc.py` runs automatically after compilation and patches the CRC32 into `firmware.bin` before flashing.

### Build Only (for OTA)

```bash
pio run
# Output binary: .pio/build/nucleo_l476rg/firmware.bin
# This binary already has CRC injected — ready for XMODEM transfer
```

---

## What You'll See on the Serial Terminal

### On Power-On (after bootloader launches app)

```
========================================
  Fancy FreeRTOS Application Started!
  CPU Clock: 80 MHz
========================================

Type 'help' for commands.
> 
```

### After typing `sensor start`

```
> sensor start
[CLI] Starting Real-Time Sensors...

[HC-SR04] Distance: 45 cm
[HC-SR04] Distance: 43 cm
[HC-SR04] Distance: 22 cm
[HC-SR04] Distance: 8 cm

[HW-201] WARNING! OBSTACLE DETECTED!
[HW-201] Obstacle Removed.
```
<img width="1919" height="1079" alt="Screenshot 2026-05-31 194216" src="https://github.com/user-attachments/assets/cbd251d2-da02-4440-b7a0-81ab5bab434f" />

### CLI Command Reference

| Command | Description | Hardware Effect |
|---------|-------------|----------------|
| `help` | List all commands | None |
| `sensor start` | Arm sensors + LED bar graph | HC-SR04 starts firing, LEDs respond to distance |
| `sensor stop` | Disarm sensors | All 4 LEDs turn off |
| `led cascade` | Knight Rider sweep pattern | LEDs sweep back and forth 3× |
| `led flash` | All LEDs blink 5× | All 4 LEDs flash simultaneously |
| `status` | Print system uptime | None |

---

## FreeRTOS Task Summary

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `SensorTask` | **4 (Highest)** | 256 words | Triggers HC-SR04, processes sensor queue |
| `ButtonMonitorTask` | **3** | 256 words | Polls physical buttons every 50ms |
| `TerminalTask` | **2** | 512 words | Parses CLI commands from UART queue |
| `LEDControllerTask` | **2** | 512 words | Manages PWM fading and IR blink override |
| `HeartbeatTask` | **1 (Lowest)** | 128 words | Blinks onboard LED every 1s |

---

## Key Source Files

| File | Description |
|------|-------------|
| [`src/main.c`](src/main.c) | All task code, ISRs, hardware init — the full application |
| [`inject_crc.py`](inject_crc.py) | Post-build Python script: reads .bin, calculates CRC32, patches AppHeader |
| [`app_offset.ld`](app_offset.ld) | Linker script: places code at 0x08008000, defines `.app_header` section |
| [`platformio.ini`](platformio.ini) | Build config: custom upload script (bootloader-aware), extra_scripts |

---

*← [Back to Bootloader](../SecureBootloader_L476/README.md)* | *← [Back to main README](../README.md)*
