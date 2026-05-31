# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

*No unreleased changes at this time.*

---

## [1.0.0] - 2026-05-30

### Added

#### System Architecture
- **Complete dual-project PlatformIO workspace**: `SecureBootloader_L476` (0x08000000–0x08007FFF, 32KB) and `FreeRTOS_App_L476` (0x08008000+) operating as independent, version-aware firmware images.
- **AppHeader struct** at fixed address `0x08008188` containing magic word (`0xAA55AA55`), firmware version, and CRC32 checksum — bridging bootloader integrity checks to application identity.

#### Secure Bootloader
- **Secure bootloader** with dual boot paths: normal boot (CRC-verified jump to application) and OTA mode (XMODEM-CRC firmware update via USART1).
- **Hardware CRC32 verification** of the full application image using the STM32 hardware CRC peripheral (eliminates software CRC overhead entirely).
- **XMODEM-CRC OTA protocol** receiver: erases flash pages 16–255, programs received data as 64-bit double-words, reboots into new firmware on successful transfer.
- **PB10 OTA trigger**: holding PB10 LOW during reset forces the bootloader into XMODEM receive mode, bypassing application boot — doubles as a hardware recovery mechanism.
- **Python post-build CRC injection script** (`inject_crc.py`): runs automatically after each PlatformIO build, computes CRC32 over the application binary, and patches the `AppHeader` struct in the `.bin` file before flashing — ensuring the bootloader always sees a valid checksum.
- **Clean peripheral teardown before jump**: all HAL peripherals disabled, SysTick stopped, interrupts globally disabled, VTOR set to `0x08008000`, MSP loaded from application vector table before branching to application Reset_Handler.

#### FreeRTOS Application
- **FreeRTOS multi-tasking with 5 concurrent tasks** running on the STM32L476RG (ARM Cortex-M4 @ 80MHz):

  | Task                  | Priority | Period   | Description                                         |
  |-----------------------|----------|----------|-----------------------------------------------------|
  | `HeartbeatTask`       | 1        | 1000ms   | Toggles PA5 onboard LED as system alive indicator   |
  | `ButtonMonitorTask`   | 3        | 50ms     | Polls PC13 (cascade) and PB10 (flash) buttons       |
  | `TerminalTask`        | 2        | Event    | Interrupt-driven serial CLI over USART1             |
  | `SensorTask`          | 4        | 500ms    | HC-SR04 trigger + sensor queue processing           |
  | `LEDControllerTask`   | 2        | Event    | PWM fading, IR blink override, queue-driven updates |

- **3 FreeRTOS inter-task queues**: `xLEDQueue` (LED commands), `xRXQueue` (UART characters from ISR), `xSensorQueue` (processed distance readings).

#### Sensor Subsystem
- **HC-SR04 ultrasonic distance sensing** with EXTI-based echo timing — rising edge starts a TIM5 capture, falling edge computes pulse width; zero CPU polling during measurement.
- **TIM5 (32-bit, 1MHz) microsecond stopwatch** for HC-SR04 echo pulse measurement — chosen specifically to avoid timer channel conflicts with PWM outputs.
- **HW-201 IR proximity detection** on PA7 with both-edge EXTI interrupt: falling edge = object detected, rising edge = object removed — enabling instantaneous IR state transitions without polling.
- **Sensor gating**: all sensor activity (HC-SR04 triggering, LED updates) is suppressed until the user sends `sensor start` via the CLI, preventing spurious output on boot.

#### LED Control
- **Hardware PWM LED bar graph** on 4 LEDs driven by three independent timers:
  - TIM3_CH1 → PB4 (LED1, nearest)
  - TIM3_CH2 → PB5 (LED2)
  - TIM2_CH2 → PB3 (LED3)
  - TIM1_CH1 → PA8 (LED4, farthest)
  - All channels: 1kHz frequency, 1000-step CCR resolution.
- **Distance-to-brightness cascade mapping**:
  - >30cm → all LEDs OFF
  - 30cm to 5cm → smooth proportional fade, LED4 lights first (farthest = soonest warning)
  - ≤5cm → all 4 LEDs at 100% brightness
- **IR priority override**: when the HW-201 IR sensor detects an object, all 4 LEDs blink synchronously at 100% duty cycle, overriding the ultrasonic bar graph — IR proximity takes precedence.

#### Serial CLI
- **Interactive serial CLI** over USART1 at 115200 baud 8N1, accessible from any terminal (Tera Term v5.6.1 recommended for OTA transfers).
- **6 CLI commands** implemented in `TerminalTask`:

  | Command        | Action                                              |
  |----------------|-----------------------------------------------------|
  | `sensor start` | Activates HC-SR04 triggering and LED bar graph      |
  | `sensor stop`  | Suspends sensor polling and turns off all LEDs      |
  | `led cascade`  | Manually triggers the distance-proportional cascade |
  | `led flash`    | Manually triggers the IR blink pattern              |
  | `status`       | Prints current sensor reading, LED state, uptime    |
  | `help`         | Lists all commands with descriptions                |

### Fixed

#### 🐛 CRITICAL — XMODEM Transfer Stuck at 128 Bytes (0.6%)

- **Symptom**: XMODEM firmware transfer via Tera Term stalled permanently after receiving exactly one 128-byte packet (0.6% progress). The STM32 appeared to lock up; only a hard reset recovered it.
- **Root Cause**: The bootloader's flash write loop cast unaligned packet data to a 64-bit pointer: `*(uint64_t*)(&packet[3 + i])`. The XMODEM packet layout places the data payload at byte offset 3 (after SOH, block number, complement), meaning `&packet[3]` is never 8-byte aligned. The ARM Cortex-M4, by default, generates a **HardFault** on unaligned 64-bit (`LDRD`/`STRD`) memory accesses — killing the bootloader silently before the second packet could be ACK'd.
- **Fix**: Replaced the pointer cast with `memcpy(&double_word, &packet[3 + i], 8)` — `memcpy` is alignment-safe by definition and compiles to byte-wise loads on ARM when alignment cannot be guaranteed.
- **Lesson**: On ARM Cortex-M4, 32-bit unaligned accesses are handled transparently by the bus matrix, but **64-bit unaligned accesses always fault**. Never cast a potentially-unaligned pointer to `uint64_t*` — always use `memcpy`.
- *(Commit: `fix: replace unaligned 64-bit flash write cast with memcpy in XMODEM receiver`)*

---

#### 🐛 HC-SR04 Constant 4cm Reading

- **Symptom**: The HC-SR04 distance sensor reported approximately 4cm regardless of actual target distance. Waving a hand in front of the sensor produced no change in the reading.
- **Root Cause**: TIM5 was configured with a prescaler value to achieve 1MHz tick rate, but the prescaler shadow register was **never loaded** because the Update Generation bit (`TIM_EGR_UG`) was not written after configuration. The TIM5 counter ran at the default post-reset prescaler (÷1), making it tick at the full 80MHz APB clock — causing the echo pulse width to compute as a tiny number, always corresponding to ~4cm.
- **Fix**: Added `TIM5->EGR = TIM_EGR_UG;` immediately after writing `TIM5->PSC`. This forces an update event, which transfers the shadow register value into the active prescaler register.
- **Lesson**: On STM32 timers, `PSC` (and `ARR`) writes go into **shadow registers** and only take effect on the next update event. After configuring a timer prescaler at runtime, always generate a software update event via `EGR |= TIM_EGR_UG` to make the setting active immediately.
- *(Commit: `fix: add TIM5 EGR update event after prescaler config to correct HC-SR04 timing`)*

---

#### 🐛 TIM2 Resource Conflict — Ultrasonic Timing vs. LED3 PWM

- **Symptom**: When LED3 PWM (PB3, TIM2_CH2) was enabled, the HC-SR04 echo timing became erratic — readings jumped between correct values and garbage. When LED3 was disabled, HC-SR04 worked correctly.
- **Root Cause**: TIM2 was being used simultaneously as the microsecond stopwatch for HC-SR04 echo measurement **and** as the PWM source for LED3 (PB3 = TIM2_CH2, AF1). Writing the PWM `CCR2` register and reconfiguring TIM2 channel modes for LED output conflicted with the ongoing timer reads used for echo timing. The same timer peripheral cannot serve two independent timing roles concurrently.
- **Fix**: Migrated the HC-SR04 microsecond stopwatch from TIM2 to **TIM5** — a 32-bit general-purpose timer on the STM32L476RG with no PWM output pins used by this project, making it a clean, conflict-free choice. TIM2 was then reserved exclusively for LED3 PWM.
- **Lesson**: Before assigning a timer to a new purpose in a multi-peripheral design, always audit all existing timer usage — including alternate function pin mappings. On the STM32L476RG, a single timer may appear on multiple pins; verify the full AF table before assuming a timer is free.
- *(Commit: `fix: migrate HC-SR04 stopwatch from TIM2 to TIM5 to resolve PWM channel conflict`)*

---

#### 🐛 Bootloader Jump Instability — Interrupt Vector Corruption After Boot

- **Symptom**: After the bootloader jumped to the application, the firmware appeared to run briefly (HeartbeatTask LED blinked once or twice) then crashed — USART1 produced garbage or nothing, and the board required a hard reset.
- **Root Cause**: The bootloader did not update the **Vector Table Offset Register (`SCB->VTOR`)** before jumping. After reset, VTOR points to `0x08000000` (bootloader vectors). When the application ran and an interrupt fired (SysTick, UART, EXTI), the CPU fetched the handler address from the bootloader's vector table — jumping into the bootloader's ISR code with the application's stack and state, immediately corrupting execution.
- **Fix**: Added `SCB->VTOR = APP_BASE_ADDR;` (i.e., `0x08008000`) at the very top of the application's `main()`, before `HAL_Init()` or any peripheral initialisation that could trigger an interrupt.
- **Lesson**: When implementing a bootloader that jumps to an application at a non-zero flash offset, updating `SCB->VTOR` in the **application** (not just the bootloader) is essential. The application cannot rely on the bootloader having done this, since it may also be flashed and debugged directly via ST-Link.
- *(Commit: `fix: set SCB->VTOR to APP_BASE_ADDR at application startup to prevent interrupt vector corruption`)*

---

[Unreleased]: https://github.com/YourUsername/STM32-FreeRTOS-Advanced-Embedded-System/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/YourUsername/STM32-FreeRTOS-Advanced-Embedded-System/releases/tag/v1.0.0
