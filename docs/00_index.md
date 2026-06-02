# docs/ Documentation Index

Welcome to the full documentation suite for the **STM32 Advanced FreeRTOS Embedded System**. Every document is written to explain concepts from first principles while providing the depth needed for real-world understanding.

> **New to embedded systems?** Start with the [Learning Path](../LEARNING_PATH.md) — it tells you exactly what to read, in what order, with milestone checkpoints at each stage.
> **Don't know a term?** Check the [Glossary](glossary.md) — every technical word used in this repo is defined in plain language.

---

## Reading Order for Beginners

If you are new to embedded systems or FreeRTOS, follow this order:

| Step | Document | Difficulty | What You'll Learn |
|------|----------|------------|-------------------|
| 1 | [System Architecture](01_system_architecture.md) | Beginner | How the two projects fit together |
| 2 | [Hardware Wiring](02_hardware_wiring.md) | Beginner | How to connect everything safely |
| 3 | [FreeRTOS Explained](03_freertos_explained.md) | Beginner | What an RTOS is and how we use it |
| 4 | [Hardware PWM](05_hardware_pwm.md) | Intermediate | How LEDs are faded with zero CPU usage |
| 5 | [Sensors & Interrupts](06_sensors_and_interrupts.md) | Intermediate | How distance and proximity are measured |
| 6 | [CLI Terminal](08_cli_terminal.md) | Intermediate | How the serial command interface works |
| 7 | [Secure Bootloader](04_secure_bootloader.md) | Advanced | How the bootloader verifies and launches the app |
| 8 | [OTA Updates / XMODEM](07_ota_xmodem.md) | Advanced | How to update firmware wirelessly over serial |
| 9 | [Performance Analysis](09_performance_analysis.md) | Advanced | Stack usage, ISR latency, memory footprint |
| 10 | [Troubleshooting](10_troubleshooting.md) | Reference | Common issues and how to fix them |

---

## Reference Files

| File | Purpose |
|------|---------|
| [Glossary](glossary.md) | Plain-language definitions of every technical term (EXTI, VTOR, PSC, XMODEM, ISR, queue, etc.) |
| [Learning Path](../LEARNING_PATH.md) | Structured 7-stage guide for learning embedded systems through this project |

---

## Document Summaries

### [01: System Architecture](01_system_architecture.md)
Flash memory map, boot sequence flowchart, FreeRTOS task diagram, interrupt routing table, queue communication map, memory usage figures.

### [02: Hardware Wiring](02_hardware_wiring.md)
Full component list, complete pin-by-pin wiring table, power rail diagram, HC-SR04 voltage divider circuit (mandatory — 5V ECHO to 3.3V MCU), LED resistor values.

### [03: FreeRTOS Explained](03_freertos_explained.md)
RTOS fundamentals from scratch, all 5 tasks with priority/stack/blocking analysis, ISR-to-task communication pattern, queue message types.

### [04: Secure Bootloader](04_secure_bootloader.md)
AppHeader struct, CRC32 hardware verification algorithm, Python CRC injection script, VTOR + MSP jump mechanism, force-update via PB10.

### [05: Hardware PWM](05_hardware_pwm.md)
PWM fundamentals, TIM1/TIM2/TIM3 configuration, alternate function routing, LED cascade distance math (30cm to 5cm), CCR register writes.

### [06: Sensors & Interrupts](06_sensors_and_interrupts.md)
HC-SR04 physics, TIM5 microsecond stopwatch, PSC shadow register bug (the 4cm story), HW-201 IR detection, dual-edge interrupt design.

### [07: OTA Updates / XMODEM](07_ota_xmodem.md)
XMODEM packet structure, CRC-16/CCITT, OTA sequence diagram, step-by-step Tera Term guide, the unaligned memory HardFault bug story.

### [08: CLI Terminal](08_cli_terminal.md)
Interrupt-driven UART, ring buffer, command parsing, full command reference, example terminal session, connection guide.

### [09: Performance Analysis](09_performance_analysis.md)
Per-task stack high-water marks, heap usage, HC-SR04 ISR latency, CPU utilization estimate (~99% idle), build size breakdown.

### [10: Troubleshooting](10_troubleshooting.md)
Every common failure mode: baud rate mismatch, CRC failure, XMODEM stall, LEDs not responding, sensor stuck readings, ST-Link errors, and more.

### [Glossary](glossary.md)
A-Z definitions of every technical term used in this project. Start here if you don't know what EXTI, VTOR, PSC, ISR, or XMODEM means.

---

*Back to [Main README](../README.md) | [Learning Path](../LEARNING_PATH.md)*
