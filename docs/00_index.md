# docs/ Documentation Index

Welcome to the full documentation suite for the **STM32 Advanced FreeRTOS Embedded System**. Every document is written to explain concepts from first principles while providing the depth needed for real-world understanding.

---

## Reading Order for Beginners

If you are new to embedded systems or FreeRTOS, follow this order:

| Step | Document | What You'll Learn |
|------|----------|-------------------|
| 1 | [System Architecture](01_system_architecture.md) | How the two projects fit together |
| 2 | [Hardware Wiring](02_hardware_wiring.md) | How to connect everything |
| 3 | [FreeRTOS Explained](03_freertos_explained.md) | What an RTOS is and how we use it |
| 4 | [Secure Bootloader](04_secure_bootloader.md) | How the bootloader verifies and launches the app |
| 5 | [Hardware PWM](05_hardware_pwm.md) | How LEDs are faded with zero CPU usage |
| 6 | [Sensors & Interrupts](06_sensors_and_interrupts.md) | How distance and proximity are measured |
| 7 | [OTA Updates / XMODEM](07_ota_xmodem.md) | How to update firmware wirelessly over serial |
| 8 | [CLI Terminal](08_cli_terminal.md) | How the serial command interface works |
| 9 | [Performance Analysis](09_performance_analysis.md) | Stack usage, ISR latency, memory footprint |
| 10 | [Troubleshooting](10_troubleshooting.md) | Common issues and how to fix them |

---

## Document Summaries

### [01 — System Architecture](01_system_architecture.md)
Flash memory map, boot sequence flowchart, FreeRTOS task diagram, interrupt routing table, queue communication map, memory usage figures.

### [02 — Hardware Wiring](02_hardware_wiring.md)
Full component list, complete pin-by-pin wiring table, power rail diagram, HC-SR04 voltage level notes, LED resistor values.

### [03 — FreeRTOS Explained](03_freertos_explained.md)
RTOS fundamentals from scratch, all 5 tasks with priority/stack/blocking analysis, ISR-to-task communication pattern, queue message types.

### [04 — Secure Bootloader](04_secure_bootloader.md)
AppHeader struct, CRC32 hardware verification algorithm, Python CRC injection script, VTOR + MSP jump mechanism, force-update via PB10.

### [05 — Hardware PWM](05_hardware_pwm.md)
PWM fundamentals, TIM1/TIM2/TIM3 configuration, alternate function routing, LED cascade distance math (30cm→5cm), CCR register writes.

### [06 — Sensors & Interrupts](06_sensors_and_interrupts.md)
HC-SR04 physics, TIM5 microsecond stopwatch, EXTI shadow register bug, HW-201 IR detection, dual-edge interrupt design.

### [07 — OTA Updates / XMODEM](07_ota_xmodem.md)
XMODEM packet structure, CRC-16/CCITT, OTA sequence diagram, step-by-step Tera Term guide, **the unaligned memory bug story**.

### [08 — CLI Terminal](08_cli_terminal.md)
Interrupt-driven UART, command parsing, full command reference, example terminal session, connection guide.

### [09 — Performance Analysis](09_performance_analysis.md)
Per-task stack high-water marks, heap usage, HC-SR04 ISR latency, CPU utilization estimate, build size breakdown.

### [10 — Troubleshooting](10_troubleshooting.md)
Most-googled issues: baud rate mismatch, CRC failure, XMODEM stuck, LEDs not responding, sensor wrong readings, and more.

---

*Back to [Main README](../README.md)*
