# Hardware Wiring Guide

> **Navigation**: [← System Architecture](01_system_architecture.md) | [← Back to README](../README.md) | [Bootloader →](03_bootloader.md)

---

## Table of Contents

1. [Components List](#1-components-list)
2. [Complete Wiring Table](#2-complete-wiring-table)
3. [HC-SR04 Ultrasonic Sensor — Special Notes](#3-hc-sr04-ultrasonic-sensor--special-notes)
4. [HW-201 IR Proximity Sensor](#4-hw-201-ir-proximity-sensor)
5. [LED Wiring Detail](#5-led-wiring-detail)
6. [Button Wiring](#6-button-wiring)
7. [Power Rail Diagram](#7-power-rail-diagram)
8. [Hardware Photos](#8-hardware-photos)
9. [Cautions and Notes](#9-cautions-and-notes)

---

## 1. Components List

| Component | Model / Spec | Qty | Purpose in Project |
|---|---|---|---|
| **MCU Development Board** | ST Nucleo-L476RG (STM32L476RGT6, ARM Cortex-M4 @ 80 MHz, 96 KB RAM, 1 MB Flash) | 1 | Main processing unit; runs bootloader + FreeRTOS application |
| **Ultrasonic Distance Sensor** | HC-SR04 (2 cm – 400 cm range, 40 kHz, **5V supply, 5V ECHO output**) | 1 | Measures object distance; drives cascade LED brightness |
| **IR Proximity Sensor** | HW-201 (adjustable sensitivity potentiometer, digital OUT, active LOW, **3.3V–5V supply**) | 1 | Detects close-range object presence; overrides LED to blink mode |
| **LEDs** | Standard 5mm through-hole LED (red or any color), Vf ≈ 2.0V | 4 | Visual distance feedback; PWM-controlled brightness |
| **Current-Limiting Resistors** | 220 Ω, 1/4W through-hole | 4 | One per LED; limits GPIO source current to safe level |
| **External Push Button** | Momentary SPST tactile switch | 1 | PB10: dual-function — triggers LED flash effect AND enters bootloader OTA mode when held at reset |
| **Pull-up Resistor** | 10 kΩ, 1/4W through-hole | 1 | External pull-up for PB10 button to 3.3V (ensures HIGH default state) |
| **Breadboard** | Half-size or full-size solderless breadboard | 1 | Mounts sensors, LEDs, button, resistors |
| **Jumper Wires** | Male-to-male and male-to-female Dupont wires | ~30 | Interconnects between Nucleo headers and breadboard |
| **USB Micro-B Cable** | Standard USB 2.0 Micro-B | 1 | Powers the Nucleo board; also provides ST-Link USB for programming/debug |

---

## 2. Complete Wiring Table

> [!IMPORTANT]
> The Nucleo-L476RG operates at **3.3V logic**. All GPIO pins are 3.3V. The HC-SR04 is a **5V device** — its ECHO pin outputs 5V logic. See [Section 3](#3-hc-sr04-ultrasonic-sensor--special-notes) and [Section 9](#9-cautions-and-notes) for the voltage divider recommendation before connecting ECHO directly.

### HC-SR04 Ultrasonic Sensor

| Signal | HC-SR04 Pin | MCU Pin | GPIO Port | Direction | Notes |
|---|---|---|---|---|---|
| Trigger pulse | TRIG | **PC7** | GPIOC | MCU → Sensor | 10 µs HIGH pulse sent by `SensorTask` every 500 ms |
| Echo return | ECHO | **PB6** | GPIOB | Sensor → MCU | **⚠️ 5V output** — use voltage divider (see Section 3). Captured by EXTI on both edges; timed with TIM5 @ 1 MHz |
| Power | VCC | **5V (CN7 pin 18)** | — | Board → Sensor | Must be 5V; sensor won't trigger reliably at 3.3V |
| Ground | GND | **GND (any)** | — | — | Common ground with Nucleo GND |

### HW-201 IR Proximity Sensor

| Signal | HW-201 Pin | MCU Pin | GPIO Port | Direction | Notes |
|---|---|---|---|---|---|
| IR output | OUT (D0) | **PA7** | GPIOA | Sensor → MCU | Active LOW — goes LOW when object detected. Triggers EXTI both edges |
| Power | VCC | **3.3V** | — | Board → Sensor | HW-201 works on 3.3V; uses 3.3V to avoid 5V logic on OUT pin |
| Ground | GND | **GND (any)** | — | — | Common ground |

> **Sensitivity adjustment**: The HW-201 has a blue potentiometer on board. Turn it clockwise to decrease detection range, counter-clockwise to increase it. Set it to detect objects at ~5–10 cm for best interaction with the ultrasonic cascade.

### LEDs (PWM Controlled)

| LED | Anode → MCU Pin | Timer Channel | GPIO Port | PWM Frequency | Notes |
|---|---|---|---|---|---|
| **LED1** (nearest, brightest at 5cm) | **PB4** | TIM3_CH1 | GPIOB | ~1 kHz | First LED to illuminate as object approaches |
| **LED2** | **PB5** | TIM3_CH2 | GPIOB | ~1 kHz | |
| **LED3** | **PB3** | TIM2_CH2 | GPIOB | ~1 kHz | Originally TIM2 — see [TIM2 conflict](../docs/05_bugs_and_lessons.md) |
| **LED4** (farthest, first to turn on) | **PA8** | TIM1_CH1 | GPIOA | ~1 kHz | First LED to light up as object enters 30 cm range |

Each LED cathode connects to GND via a **220 Ω resistor**. The MCU pin drives the anode (PWM HIGH = LED on).

### Buttons

| Button | MCU Pin | GPIO Port | Active State | Pull | Notes |
|---|---|---|---|---|---|
| **Blue Onboard Button** | **PC13** | GPIOC | LOW (active LOW, pulled HIGH internally) | Internal pull-up (board hardware) | Triggers `led cascade` behavior in `ButtonMonitorTask` |
| **External Breadboard Button** | **PB10** | GPIOB | LOW (pressed = pulled to GND) | **External 10kΩ to 3.3V** | Dual-function: normal press → `led flash`; held at reset → OTA bootloader mode |

### UART (Serial CLI)

| Signal | MCU Pin | GPIO Port | Direction | Notes |
|---|---|---|---|---|
| **TX** | **PA9** | GPIOA | MCU → PC | UART1 transmit; connect to RX of USB-Serial adapter or use ST-Link VCP |
| **RX** | **PA10** | GPIOA | PC → MCU | UART1 receive; interrupt-driven (RXNE flag), bytes posted to `xRXQueue` |
| **Baud rate** | — | — | — | 115200 baud, 8 data bits, no parity, 1 stop bit (8N1) |

> **Using ST-Link VCP**: The Nucleo board's ST-Link includes a Virtual COM Port that connects to PA2/PA3 by default via solder bridges. If UART1 (PA9/PA10) is preferred (as in this project), connect a USB-Serial adapter externally, or reconfigure the ST-Link solder bridges (SB13/SB14 on the Nucleo board) — see the Nucleo-L476RG User Manual UM1724.

### Onboard Resources Used

| Resource | MCU Pin | Notes |
|---|---|---|
| **Onboard LED (LD2)** | **PA5** | Toggled by `HeartbeatTask` every 1s — alive indicator |
| **ST-Link SWDIO** | **PA13** | SWD debug/flash interface (shared with ST-Link chip on board) |
| **ST-Link SWDCLK** | **PA14** | SWD clock |

---

## 3. HC-SR04 Ultrasonic Sensor — Special Notes

### How It Works

The HC-SR04 measures distance by emitting an ultrasonic burst and measuring how long it takes for the echo to return:

1. MCU asserts TRIG pin HIGH for at least **10 µs**
2. Sensor emits eight 40 kHz pulses
3. Sensor pulls ECHO pin HIGH — MCU captures timestamp using **TIM5** (1 MHz free-running, so 1 count = 1 µs)
4. Sound travels to object and back; sensor pulls ECHO LOW
5. MCU captures end timestamp
6. Pulse width in µs → distance: `distance_cm = pulse_us / 58`

```
Distance formula:
  Speed of sound ≈ 343 m/s = 0.0343 cm/µs
  Sound travels TO the object AND BACK → divide by 2
  distance_cm = pulse_us × 0.0343 / 2
              = pulse_us / 58.3
              ≈ pulse_us / 58   (integer approximation)
```

### ⚠️ The 5V ECHO Voltage Problem

The HC-SR04 **ECHO pin outputs 5V** because the sensor is powered from 5V and its output stage swings rail-to-rail. The STM32L476's GPIO input threshold for 3.3V operation is typically Vil_max = 1.17V and Vih_min = 2.13V — but the **absolute maximum voltage on any GPIO pin is VDD + 0.3V = 3.6V**.

Connecting a 5V signal directly to a 3.3V GPIO pin **can permanently damage the STM32**.

### Recommended Fix: Voltage Divider

Use a resistor divider to scale 5V → ~3.0V before connecting to PB6:

```
HC-SR04 ECHO (5V)
        │
       [R1 = 10kΩ]
        │
        ├─────────────→ PB6 (STM32, ~2.94V HIGH)
        │
       [R2 = 20kΩ]
        │
       GND

Vout = 5V × (20kΩ / (10kΩ + 20kΩ)) = 5V × 0.667 = 3.33V ≈ 3.3V ✓
```

Alternatively, a **1kΩ + 2kΩ** divider works equally well for smaller resistor values (lower source impedance, faster edge response). The divider must be sized to switch cleanly during the echo pulse — avoid values above 100kΩ total (slow edge due to parasitic capacitance).

> [!CAUTION]
> **Do not skip the voltage divider.** Running ECHO directly into PB6 at 5V may appear to work initially due to the STM32's internal protection diodes clamping the voltage — but these diodes have limited current handling (~10mA) and will degrade or fail over time. Use a proper voltage divider or a dedicated level-shifter IC (e.g., TXB0104, BSS138 FET).

---

## 4. HW-201 IR Proximity Sensor

The HW-201 uses an IR LED emitter and a phototransistor receiver. When the reflected IR intensity exceeds a threshold (set by the onboard potentiometer), the open-collector `OUT` pin is pulled LOW by an LM393 comparator.

### Electrical Behavior

| Condition | OUT Pin State | Logic Level | EXTI Trigger |
|---|---|---|---|
| No object detected | HIGH (pulled to VCC by internal resistor) | Logic 1 | Rising edge → `IR_OBJECT_REMOVED` |
| Object detected | LOW (comparator output active) | Logic 0 | Falling edge → `IR_OBJECT_DETECTED` |

The signal is **active LOW** and the STM32's EXTI is configured to trigger on **both edges** (rising + falling) so the application can react to both object arrival and object removal.

### Powering at 3.3V

Powering the HW-201 from 3.3V (not 5V) keeps the OUT pin output swing at 3.3V, eliminating any level-shifting concerns. The sensor's LM393 comparator functions correctly at 3.3V supply; sensitivity range is slightly reduced compared to 5V operation but remains adequate for 5–15 cm detection.

---

## 5. LED Wiring Detail

### Circuit Topology

Each LED is wired as follows:

```
MCU GPIO Pin (PWM output)
        │
        │  (no series resistor on MCU side — resistor is on cathode side)
        │
      LED Anode (+)
      LED Cathode (-)
        │
      [220Ω resistor]
        │
       GND
```

### PWM Duty Cycle → Brightness

All four LED timer channels use an **ARR (Auto-Reload Register) of 1000** and a prescaler that produces approximately 1 kHz PWM frequency at 80 MHz core clock.

| CCR Value | Duty Cycle | LED Brightness |
|---|---|---|
| 0 | 0% | Off |
| 250 | 25% | Dim |
| 500 | 50% | Half brightness |
| 750 | 75% | Bright |
| 1000 | 100% | Full brightness |

The `LEDControllerTask` writes directly to the timer Compare/Capture Register:

```c
// Example: set LED1 (TIM3_CH1, PB4) to 75% brightness
TIM3->CCR1 = 750;   // Out of ARR=1000

// Set LED4 (TIM1_CH1, PA8) to full brightness
TIM1->CCR1 = 1000;

// Turn off LED2 (TIM3_CH2, PB5)
TIM3->CCR2 = 0;
```

### Distance → LED Cascade Mapping

The ultrasonic sensor drives a smooth cascade across all four LEDs:

| Distance | LED4 (PA8) | LED3 (PB3) | LED2 (PB5) | LED1 (PB4) | Duty |
|---|---|---|---|---|---|
| > 30 cm | OFF | OFF | OFF | OFF | 0% |
| 25 – 30 cm | ON | OFF | OFF | OFF | Proportional |
| 20 – 25 cm | ON | ON | OFF | OFF | Proportional |
| 10 – 20 cm | ON | ON | ON | OFF | Proportional |
| 5 – 10 cm | ON | ON | ON | ON | Proportional |
| ≤ 5 cm | FULL | FULL | FULL | FULL | 100% |

The duty cycle within each active LED is linearly interpolated across the distance range, creating a smooth dimming effect as the object approaches.

### IR Override (Higher Priority)

When the HW-201 detects an object (OUT goes LOW), `IR_OBJECT_DETECTED` is posted to `xLEDQueue` from ISR context. The `LEDControllerTask` immediately overrides the distance-based state — **all four LEDs blink at 100% duty** (e.g., 500ms on / 500ms off cycle) regardless of what the ultrasonic sensor reports. When the object is removed, `IR_OBJECT_REMOVED` restores ultrasonic-driven mode.

> [!NOTE]
> The IR blink override is intentionally higher priority than the ultrasonic cascade. The IR sensor is designed for close-contact detection (< 10 cm), which should always be visually distinct regardless of what the distance sensor measures.

### GPIO Current Limit

At 3.3V and Vf = 2.0V for a typical red LED:
```
I = (Vcc - Vf) / R = (3.3V - 2.0V) / 220Ω = 1.3V / 220Ω ≈ 5.9mA
```

The STM32L476's GPIO **source/sink current limit is 25mA per pin** and **80mA total across all I/O pins** (package limit). At 5.9mA per LED × 4 LEDs = **23.6mA total** — well within the safe operating area.

---

## 6. Button Wiring

### Blue Onboard Button (PC13)

The Nucleo-L476RG has a blue USER button permanently connected to PC13. The board hardware includes a pull-up resistor, so PC13 reads HIGH at rest and LOW when pressed. No external resistors are needed. The `ButtonMonitorTask` polls this pin every 50ms.

**Function**: Short press → posts `CMD_CASCADE` to `xLEDQueue`, cycling through cascade LED patterns.

### External Breadboard Button (PB10)

```
3.3V ──┬── [10kΩ pull-up] ──┬── PB10 (STM32)
       │                    │
       │              [Button SW1]
       │                    │
      GND ─────────────────GND
```

When the button is **not pressed**: PB10 = HIGH (3.3V through pull-up resistor).  
When the button is **pressed**: PB10 = LOW (shorted to GND through button).

The 10kΩ pull-up ensures a clean, defined HIGH level at rest. Without the pull-up, PB10 would float (undefined voltage) when the button is open, causing spurious triggers.

> [!IMPORTANT]
> This button's state is read by the **bootloader** (not just the application). If PB10 is LOW at power-on reset (i.e., held down while resetting), the bootloader enters **XMODEM OTA mode**. Releasing it before reset keeps normal boot behavior. This is why the pull-up resistor is critical — without it, a floating PB10 could accidentally trigger OTA mode on every reset.

**Application function**: Short press (not during reset) → posts `CMD_FLASH` to `xLEDQueue` — all LEDs flash rapidly three times.

---

## 7. Power Rail Diagram

```
USB Micro-B ──→ ST-Link USB ──→ Nucleo Board
                                     │
                    ┌────────────────┤
                    │                │
                   5V rail         3.3V rail
                    │                │
         ┌──────────┘     ┌──────────┴──────────┐
         │                │                     │
    HC-SR04 VCC      HW-201 VCC         LEDs, Buttons,
    (TRIG signal      (OUT pin           MCU GPIO, all
     goes TO MCU       stays at           3.3V logic
     at 3.3V)          3.3V — safe)
         │
     ECHO pin ──[Voltage Divider]──→ PB6 (3.3V after divider)

Power Budget (approximate):
  HC-SR04:      ~15mA @ 5V  (when transmitting)
  HW-201:       ~20mA @ 3.3V (IR LED + comparator)
  4× LEDs:      ~24mA @ 3.3V (4 × 5.9mA @ 100% duty)
  STM32L476RG:  ~10mA @ 3.3V (CPU @ 80MHz + peripherals)
  ─────────────────────────────────────────────────
  Total 5V:     ~15mA (only HC-SR04)
  Total 3.3V:   ~54mA (all other components)
  USB can supply 500mA — well within budget.
```

The Nucleo board's on-board **LDO regulator** (LD39050PU33R, 500mA max) powers the 3.3V rail from the 5V USB input. Ensure the USB host (PC) provides adequate current if additional peripherals are added.

---

## 8. Hardware Photos

> 📸 **[Hardware Photo: Full breadboard assembly — Nucleo board, HC-SR04, HW-201, 4 LEDs, button]**
> *(Contribute one via PR! Shows the complete wired-up system on a breadboard.)*

> 📸 **[Hardware Photo: Close-up of voltage divider circuit for HC-SR04 ECHO pin]**
> *(Contribute one via PR! 10kΩ and 20kΩ resistors between ECHO and PB6.)*

> 📸 **[Hardware Photo: HC-SR04 sensor close-up with TRIG/ECHO labeled]**
> *(Contribute one via PR! Shows which header pin is TRIG vs. ECHO.)*

> 📸 **[Hardware Photo: HW-201 IR sensor with potentiometer visible]**
> *(Contribute one via PR! Shows the sensitivity adjustment potentiometer.)*

> 📸 **[Hardware Photo: Tera Term XMODEM OTA update in progress]**
> *(Contribute one via PR! Shows the terminal during a firmware transfer.)*

---

## 9. Cautions and Notes

### ⚠️ Voltage Level Mixing (3.3V vs. 5V)

This is the single most important hardware caution in this project.

| Device | Supply | Output Logic | Safe for STM32 GPIO? |
|---|---|---|---|
| HC-SR04 TRIG input | 5V device | Accepts 3.3V HIGH as valid input | ✅ Yes — 3.3V MCU output is recognized as HIGH by 5V sensor |
| HC-SR04 ECHO output | 5V device | **Outputs 5V HIGH** | ❌ **No — must use voltage divider** |
| HW-201 OUT | 3.3V supply | Outputs 3.3V HIGH | ✅ Yes — safe at 3.3V |
| External button | 3.3V pull-up | — | ✅ Yes |

**The HC-SR04 TRIG pin is safe to drive directly from the MCU** (3.3V MCU output → 5V device input). The 5V HC-SR04 sees 3.3V as a valid HIGH because its input threshold is well below 3.3V.

**The HC-SR04 ECHO pin is NOT safe** to connect directly to the STM32 GPIO. Always use the voltage divider described in [Section 3](#3-hc-sr04-ultrasonic-sensor--special-notes).

### ⚠️ STM32 GPIO Maximum Ratings

| Parameter | Value |
|---|---|
| Absolute maximum voltage on any GPIO (5V-tolerant pins) | 5.5V |
| Absolute maximum voltage on non-5V-tolerant pins (e.g., PA8) | VDD + 0.3V = 3.6V |
| Max source/sink current per GPIO pin | 25mA |
| Max total I/O current (all pins combined) | 80mA |

> [!WARNING]
> **PA8 (LED4, TIM1_CH1) is NOT 5V tolerant on the STM32L476.** Check the STM32L476 datasheet Table 16 (I/O static characteristics) and the pin table for FT (5V Tolerant) markings before connecting any signal. PB6 is 5V tolerant (FT), which is why it was chosen for ECHO — but the voltage divider is still recommended as good practice.

### ⚠️ Shared EXTI Line Caution

PB6 and PA7 both fall on the `EXTI9_5` shared interrupt line. The ISR must **always check the pending flag** (`EXTI->PR1`) before acting on a signal, and **always clear it** after handling. Failing to clear a pending flag causes the ISR to re-enter immediately in an infinite loop, starving all tasks.

### ⚠️ HC-SR04 Minimum Measurement Interval

The HC-SR04 needs at least **60ms** between trigger pulses to allow the echo from the previous measurement to fully decay (to avoid ghost echoes from reflections). This project uses a **500ms** interval (generous), which is well above the minimum and avoids any interference.

### ⚠️ PB10 Button at Reset = OTA Mode

If PB10 is held LOW at power-on or hardware reset, the **bootloader enters XMODEM OTA mode**. Be aware of this when wiring or debugging — accidentally shorting PB10 to GND during a reset will put the device in OTA mode rather than launching the application. The 10kΩ pull-up must be correctly installed.

### ✅ Safe GPIO Drive Strength

The default STM32 GPIO output speed (LOW speed, 2MHz slew rate) is adequate for all signals in this project:
- TRIG pulse: 10µs width — well above any slew rate concern
- PWM LEDs: 1kHz frequency — easily driven at low speed
- UART TX: 115200 baud → bit period = 8.7µs — low speed sufficient

No pins need to be configured for HIGH or VERY HIGH speed (which would increase EMI and power consumption unnecessarily).

---

> **Navigation**: [← System Architecture](01_system_architecture.md) | [← Back to README](../README.md) | [Bootloader →](03_bootloader.md)
