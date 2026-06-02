# Learning Path

> **You built something similar and want to understand this repo deeply? Start here.**
> This file tells you exactly what to read, in what order, and what to build at each milestone to confirm you understood it.

---

## Who This Is For

- You know basic C (variables, functions, pointers, structs)
- You have blinked an LED on an STM32 or Arduino before
- You want to understand how a *real* embedded system is built, not just copy code

If you have never written a line of C, start with a C basics course first, then come back.

---

## Stage 1: Understand the Full Picture (Day 1)

**Goal:** Before reading any code, understand what the system does and how all the pieces connect.

### Read in this order:
1. The main [`README.md`](../README.md): read every section, don't skip anything
2. [`docs/00_index.md`](00_index.md): the navigation map for all 11 docs
3. [`docs/01_system_architecture.md`](01_system_architecture.md): the full system map

### Milestone Check:
Can you answer these?
- [ ] How many tasks does the FreeRTOS application have? What does each one do?
- [ ] What happens when you press the RESET button while holding PB10?
- [ ] What is the difference between `xSensorQueue` and `xLEDQueue`?
- [ ] At what Flash address does the bootloader live? Where does the application start?

If you can answer all 4, move to Stage 2.

---

## Stage 2: Understand the Hardware (Day 2)

**Goal:** Be able to wire the full circuit yourself from the pin map, without copying from a photo.

### Read in this order:
1. [`docs/02_hardware_wiring.md`](02_hardware_wiring.md): full wiring guide

### Key concepts to understand:
- Why does the HC-SR04 ECHO pin need a voltage divider but the TRIG pin does not?
- Why does the HC-SR04 need 5V but the HW-201 works on 3.3V?
- What happens if you use the wrong baud rate in your terminal?

### Milestone Check:
- [ ] Wire the full circuit (Nucleo + HC-SR04 + HW-201 + 4 LEDs + button)
- [ ] Flash the bootloader and application following the Quick Start
- [ ] Type `sensor start` and see real distance readings change as you move your hand

If the LEDs respond to your hand's distance, you are wired correctly. Move to Stage 3.

---

## Stage 3: Understand FreeRTOS (Days 3-4)

**Goal:** Understand why this project uses an RTOS instead of a big `while(1)` loop.

### Read in this order:
1. [`docs/03_freertos_explained.md`](03_freertos_explained.md): tasks, queues, priorities, scheduling

### Key concepts to understand:
- What is a context switch and when does it happen?
- Why can't you use a global variable instead of a queue between tasks?
- What does `vTaskDelay(pdMS_TO_TICKS(500))` actually do while it waits?
- Why does `SensorTask` have a higher priority than `HeartbeatTask`?

### Milestone Check:
- [ ] In your own words, explain what happens between the moment an echo pulse arrives on PB6 and the moment an LED changes brightness. Trace every step through the task and queue system.
- [ ] Look at `main.c` in `FreeRTOS_App_L476/src/` and find where the 5 tasks are created. What stack size is each given?

---

## Stage 4: Understand the Sensors and Interrupts (Days 5-6)

**Goal:** Understand how hardware interrupts work at the register level and how they hand data to the RTOS safely.

### Read in this order:
1. [`docs/06_sensors_and_interrupts.md`](06_sensors_and_interrupts.md): HC-SR04, HW-201, EXTI, TIM5

### Key concepts to understand:
- What is the difference between polling a pin and using an interrupt?
- Why does TIM5 run at 1 MHz specifically? What would happen at 80 MHz?
- Why does the EXTI handler call `portYIELD_FROM_ISR` at the end?
- What does `__HAL_GPIO_EXTI_CLEAR_IT` do and why is it mandatory?

### The Bug to Study:
Read Bug #2 in the README (HC-SR04 always returning 4cm). This is the shadow register bug.
- Why did writing `TIM5->PSC = 79` not immediately change the timer speed?
- What does `TIM5->EGR = TIM_EGR_UG` actually do at the hardware level?

### Milestone Check:
- [ ] Disconnect the HC-SR04 ECHO wire and explain what happens to the FreeRTOS task system (does it crash? hang? keep running?)
- [ ] Find `EXTI9_5_IRQHandler` in `main.c`. Trace exactly what happens when a rising edge arrives on PB6.

---

## Stage 5: Understand Hardware PWM (Day 7)

**Goal:** Understand how the LED bar graph works using hardware timers: no software blinking.

### Read in this order:
1. [`docs/05_hardware_pwm.md`](05_hardware_pwm.md): timers, PWM channels, duty cycle

### Key concepts to understand:
- What are PSC and ARR and how do they set the PWM frequency?
- What does CCR control and how does it control LED brightness?
- Why is hardware PWM "zero CPU waste" compared to software PWM?
- Why do TIM1, TIM2, and TIM3 all need `Period = 1000` for the LEDs to fade smoothly?

### Milestone Check:
- [ ] Calculate: if PSC=79 and ARR=1000, what is the PWM frequency on the STM32L476RG (80 MHz clock)?
- [ ] Type `led flash` in the terminal. All 4 LEDs blink at 100% duty cycle. What CCR value achieves 100%?

---

## Stage 6: Understand the Bootloader (Days 8-9)

**Goal:** Understand how a secure bootloader works at the register level: the hardest and most valuable part of this project.

### Read in this order:
1. [`docs/04_secure_bootloader.md`](04_secure_bootloader.md): CRC verification, XMODEM, Flash programming, VTOR/MSP jump
2. [`docs/07_ota_xmodem.md`](07_ota_xmodem.md): the full XMODEM protocol and OTA flow

### Key concepts to understand:
- What is VTOR and why must the bootloader set it before jumping to the application?
- What is the MSP and what happens if the bootloader doesn't set it correctly before the jump?
- Why must all bootloader peripherals be de-initialised before jumping?
- What is the AppHeader struct and what fields does it contain?
- What does `inject_crc.py` do and at what point in the build process does it run?

### The Bug to Study:
Read Bug #1 in the README (XMODEM unaligned access HardFault).
- Why is `*(uint64_t*)(&packet[3])` dangerous on Cortex-M4?
- Why does `memcpy` solve the alignment problem?

### Milestone Check:
- [ ] Perform a full OTA update from scratch: build the application, hold PB10, press reset, watch `CCCCC...`, send firmware via XMODEM in Tera Term, watch the board reboot into the new firmware.
- [ ] Intentionally corrupt your firmware binary (open it in a hex editor and change one byte), attempt to boot, and observe the bootloader reject it and enter OTA mode.

---

## Stage 7: Understand the CLI and Performance (Day 10)

**Goal:** Understand how interrupt-driven UART input works and how the full system performs.

### Read in this order:
1. [`docs/08_cli_terminal.md`](08_cli_terminal.md): interrupt-driven UART, ring buffer, CLI parser
2. [`docs/09_performance_analysis.md`](09_performance_analysis.md): stack, CPU load, ISR latency

### Key concepts to understand:
- How does a byte typed in Tera Term end up executing a command in `TerminalTask`?
- What is the role of `xRXQueue` in decoupling the UART ISR from the CLI logic?
- What percentage of CPU time does the system spend sleeping in `__WFI`?

### Milestone Check:
- [ ] Add a new CLI command `uptime` that prints how long the system has been running. It only needs to print a number: you already have FreeRTOS tick count available via `xTaskGetTickCount()`.
- [ ] Type your new command in Tera Term and verify it works.

If you completed Stage 7 including the `uptime` command, you have not just read this project: you have extended it. That is the real milestone.

---

## Troubleshooting While Learning

If something is not working, read [`docs/10_troubleshooting.md`](10_troubleshooting.md) first. Every common failure mode is documented with its root cause and fix.

If your issue is not in the troubleshooting doc, open a [GitHub Issue](https://github.com/HeetYadav/STM32-FreeRTOS-Advanced-Embedded-System/issues) with:
- What you expected to happen
- What actually happened
- Your serial terminal output

---

## Glossary

Every technical term used in this project is defined in plain language in [`docs/glossary.md`](docs/glossary.md).

If you encounter a word you don't know (EXTI, VTOR, PSC, ISR, XMODEM, etc.), look there first.

---
