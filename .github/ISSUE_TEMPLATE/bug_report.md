---
name: Bug Report
about: Report a bug or unexpected behaviour
title: '[BUG] '
labels: bug
assignees: ''
---

## Describe the Bug

<!-- A clear and concise description of what the bug is. -->
<!-- Example: "HC-SR04 always reads 0cm after calling `sensor start`." -->

## Hardware Setup

<!-- Tell us exactly what you have connected and how. -->

**Board**: STM32 Nucleo-L476RG (or describe your deviation)

| Component             | Connected To  | Notes                          |
|-----------------------|---------------|--------------------------------|
| HC-SR04 TRIG          | PC7           |                                |
| HC-SR04 ECHO          | PB6           |                                |
| HW-201 IR OUT         | PA7           |                                |
| LED1                  | PB4           | TIM3_CH1                       |
| LED2                  | PB5           | TIM3_CH2                       |
| LED3                  | PB3           | TIM2_CH2                       |
| LED4                  | PA8           | TIM1_CH1                       |
| External Button       | PB10          |                                |
| UART TX               | PA9           | 115200 baud 8N1                |
| UART RX               | PA10          |                                |

<!-- Add or remove rows as needed. Note any differences from the above defaults. -->

**Power supply**: <!-- e.g., USB from ST-Link / external 5V / bench supply -->

**Any modifications to the default schematic?**
<!-- Yes/No — if yes, describe -->

## Steps to Reproduce

<!-- Be precise. List the exact sequence that triggers the bug. -->

1. Flash the firmware using `...`
2. Open Tera Term at `115200 baud 8N1` on `COMx`
3. Send the command `...`
4. Observe `...`

## Expected Behaviour

<!-- What should happen? -->

## Actual Behaviour

<!-- What actually happens? Be specific — "it doesn't work" is hard to debug. -->

## Serial Terminal Output

<!-- Paste the full Tera Term / serial output here. Use Tera Term's "Log" feature to capture a clean transcript. -->

```
Paste serial output here
```

## Build Environment

| Field                   | Value                                  |
|-------------------------|----------------------------------------|
| PlatformIO Core version | <!-- run `pio --version` -->           |
| PlatformIO IDE version  | <!-- VS Code extension version -->     |
| framework-stm32cubel4   | <!-- check `.pio/libdeps` or `platformio.ini` --> |
| FreeRTOS library version| <!-- check `platformio.ini` -->       |
| ARM GCC version         | <!-- run `arm-none-eabi-gcc --version` --> |
| Host OS                 | <!-- e.g., Windows 11, Ubuntu 22.04 --> |
| Project (which one?)    | <!-- SecureBootloader_L476 / FreeRTOS_App_L476 / Both --> |

## Additional Context

<!-- 
Anything else that might help:
- Logic analyser / oscilloscope captures
- Photos of your hardware setup
- Does it happen every time, or intermittently?
- Did it ever work? What changed?
- Any other peripherals connected that aren't listed above?
-->
