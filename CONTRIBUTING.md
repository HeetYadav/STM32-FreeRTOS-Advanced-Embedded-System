# Contributing to STM32-FreeRTOS-Advanced-Embedded-System

First off: thank you for taking the time to contribute! 

This project is a real-hardware, real-firmware embedded systems showcase built on the STM32L476RG Nucleo board. Contributions that improve its clarity, correctness, completeness, or educational value are genuinely welcome: whether you're a seasoned firmware engineer or someone just getting started with FreeRTOS and STM32.

---

## Table of Contents

- [What Kinds of Contributions Are Welcome](#what-kinds-of-contributions-are-welcome)
- [Ways to Contribute](#ways-to-contribute)
  - [Bug Reports](#bug-reports)
  - [Hardware Photos  HIGH VALUE](#hardware-photos--high-value)
  - [Documentation Improvements](#documentation-improvements)
  - [New FreeRTOS Task Ideas](#new-freertos-task-ideas)
  - [New CLI Commands](#new-cli-commands)
- [Pull Request Process](#pull-request-process)
- [Code Style Guide](#code-style-guide)
- [Embedded Systems Contribution Notes](#embedded-systems-contribution-notes)
- [Questions](#questions)

---

## What Kinds of Contributions Are Welcome

Almost everything! This project benefits from many types of contributions:

| Contribution Type              | Value  | Notes                                                         |
|-------------------------------|--------|---------------------------------------------------------------|
| Hardware photos                |  | The single highest-impact contribution you can make            |
| Bug fixes (firmware)           |  | Especially timing, RTOS, or flash-related issues               |
| Documentation improvements     |  | Clarifications, diagrams, corrections                         |
| New FreeRTOS task ideas        |    | Must be self-contained, not break existing task communication  |
| New CLI commands               |    | Follow the existing `TerminalTask` parsing pattern             |
| Wiring corrections             |    | If a pin label, timer channel, or AF mapping is wrong          |
| Tested timing / distance data  |    | Real measurements from your hardware setup                    |
| Bootloader / OTA improvements  |    | XMODEM robustness, CRC edge cases                             |
| PlatformIO / toolchain fixes   |      | `platformio.ini`, build flags, post-build script              |

---

## Ways to Contribute

### Bug Reports

If you found something that doesn't work: a wrong register write, a sensor that always reads 0, a CLI command that hangs: please open an issue using the **Bug Report** template.

Before filing a bug:
1. Check the [existing issues](https://github.com/HeetYadav/STM32-FreeRTOS-Advanced-Embedded-System/issues) to avoid duplicates.
2. Make sure you're on the latest `main` branch.
3. Capture your **serial terminal output** (Tera Term at 115200 baud): it's the fastest way to diagnose firmware-level issues.

---

### Hardware Photos  HIGH VALUE

**This is the single most impactful contribution you can make to this repository.**

Photos of real hardware build credibility. They let other engineers visually verify their wiring, understand the scale of the project, and see that this is a real, working system: not just code on a page.

#### What photos are needed:

- Full breadboard layout showing all connected components
- Close-up of the STM32 Nucleo-L476RG board
- HC-SR04 sensor wired to PC7 (TRIG) and PB6 (ECHO)
- HW-201 IR sensor wired to PA7
- LED bar graph (4 LEDs) with PWM wiring visible
- Serial terminal showing CLI in action (`status`, `sensor start`, `led cascade`)
- Bootloader XMODEM transfer in progress (Tera Term)
- Any angle that shows the full system assembled

#### How to contribute photos:

1. Take clear, well-lit photos (natural light or diffused LED light works well).
2. Place image files in the `docs/images/` directory.
   - Use descriptive filenames: `breadboard_full_overview.jpg`, `hcsr04_wiring_closeup.jpg`
   - Preferred formats: `.jpg` or `.png`, max ~2MB per image
3. Reference them in [`docs/hardware_wiring.md`](docs/02_hardware_wiring.md) by replacing the relevant placeholder:
   ```markdown
   >  **[Hardware Photo: Full breadboard layout]** *(Contribute one via PR!)*
   ```
   with:
   ```markdown
   ![Full breadboard layout](https://github.com/HeetYadav/STM32-FreeRTOS-Advanced-Embedded-System)
   ```
4. Open a PR with the title: `docs: add hardware photo: [brief description]`

No photo is too simple. Even a top-down phone photo beats a blank placeholder.

---

### Documentation Improvements

The documentation lives in the `docs/` folder and the root-level `README.md`. If you spot:

- A wiring error (wrong pin, wrong timer channel)
- A missing explanation or unclear sentence
- A Mermaid diagram that could be improved
- A section that's out of date with the code

please open a PR or an issue. Documentation bugs in embedded systems are real bugs: they waste hours of debugging time for the next person.

---

### New FreeRTOS Task Ideas

New tasks are very welcome, as long as they:

- Are self-contained within their own `.c`/`.h` file pair
- Communicate with other tasks **only via FreeRTOS queues**: no shared global state without a mutex
- Do not starve existing tasks (be mindful of priority levels and blocking delays)
- Are documented with their stack size, priority, and queue communication model
- Include a timing analysis in the PR description (worst-case blocking time)

**Ideas that would fit this project well:**
- Temperature/humidity sensing via DHT11 or I2C sensor
- OLED display task (SSD1306 over I2C, showing distance and status)
- Watchdog task using the IWDG peripheral
- Data logging task (ring buffer over UART)
- PWM buzzer task (beep frequency proportional to distance)

---

### New CLI Commands

The serial CLI is implemented in `TerminalTask` in `src/terminal_task.c`. New commands follow the existing pattern:

1. Add the command string to the `commands[]` array.
2. Add a handler function `cmd_yourcommand(void)`.
3. Update the `help` command output to list the new command.
4. Document the command in [`docs/cli_reference.md`](docs/08_cli_terminal.md) (or equivalent).

Commands must be non-blocking: they should not `vTaskDelay` for more than a few milliseconds inside the handler.

---

## Pull Request Process

1. **Fork** the repository to your own GitHub account.

2. **Create a feature branch** from `main`:
   ```bash
   git checkout -b feat/your-feature-name
   # or
   git checkout -b fix/what-you-are-fixing
   # or
   git checkout -b docs/what-you-are-documenting
   ```

3. **Make your changes.** Keep commits focused: one logical change per commit.

4. **Write a descriptive commit message** following the pattern:
   ```
   type(scope): short imperative description

   Longer explanation of WHY this change was made, not just WHAT it does.
   Reference any relevant register, datasheet section, or FreeRTOS API.
   ```
   Examples:
   ```
   fix(bootloader): replace unaligned 64-bit cast with memcpy to prevent HardFault
   feat(cli): add 'led pwm <value>' command to manually set brightness
   docs(wiring): add hardware photos for HC-SR04 and IR sensor connections
   ```

5. **Open a Pull Request against `main`.** Fill in the PR template: describe what changed, why, and how you tested it.

6. If your change touches firmware: **include serial terminal output** showing the system working before and after your change. Screenshot or paste from Tera Term.

---

## Code Style Guide

Follow the existing C style used throughout the project:

| Element              | Convention              | Example                         |
|---------------------|-------------------------|---------------------------------|
| Variables            | `snake_case`            | `echo_start_time`, `distance_cm` |
| Functions            | `snake_case`            | `hcsr04_trigger()`, `led_set_pwm()` |
| Types / Structs      | `PascalCase`            | `AppHeader_t`, `SensorData_t`   |
| Macros / Constants   | `ALL_CAPS`              | `HCSR04_MAX_DISTANCE_CM`, `APP_BASE_ADDR` |
| FreeRTOS handles     | `x` prefix              | `xSensorQueue`, `xLEDQueue`     |
| Task functions       | `PascalCase` + `Task`   | `SensorTask()`, `HeartbeatTask()` |
| HAL callbacks        | STM32 HAL naming        | `HAL_GPIO_EXTI_Callback()`      |

**Other style rules:**
- Add a comment on every non-obvious register write. A reader should not need the reference manual to understand your intent.
- No magic numbers: use named `#define` constants for all hardware addresses, distances, and timing values.
- Keep task functions short: complex logic belongs in helper functions, not inline inside the task loop.
- Do not use `malloc`/`free`: use FreeRTOS static allocation or stack variables only.

---

## Embedded Systems Contribution Notes

If your contribution adds or modifies **hardware connections**, please:

1. **Update the wiring table** in [`docs/hardware_wiring.md`](docs/02_hardware_wiring.md) with:
   - MCU pin
   - Alternate function (AF) number if applicable
   - Timer/channel if PWM
   - Signal direction (input/output)
   - Pull-up/pull-down configuration

2. **Include tested values** where applicable:
   - For distance sensors: your measured distance vs. reported distance at 5cm, 15cm, 30cm intervals
   - For timing signals: oscilloscope capture or logic analyser output if available
   - For PWM: measured frequency and duty cycle at key CCR values

3. **Note any STM32 peripheral conflicts.** The STM32L476RG alternate function map is dense: always cross-check against Table 16:17 of the STM32L476 datasheet (DS11585) before claiming a pin/timer combination is free.

---

## Questions

Not sure if something is a bug or expected behaviour? Not sure how a part of the firmware works? Open an issue using the **Question** template: questions are welcome and help improve the documentation for everyone.

---

*Thank you for contributing. Every photo, fix, and explanation makes this project more useful to the next embedded engineer who finds it.*
