# CLI Terminal: The Serial Command Interface

> **Navigation**: [â† 07 Button & GPIO](07_button_gpio.md) | [Home](../README.md) | [09 Debugging Guide â†’](09_debugging.md)

---

## Table of Contents

1. [How the CLI Works](#1-how-the-cli-works)
2. [Why Interrupt-Driven UART](#2-why-interrupt-driven-uart)
3. [The USART1_IRQHandler Walkthrough](#3-the-usart1_irqhandler-walkthrough)
4. [Command Reference](#4-command-reference)
5. [Example Session](#5-example-session)
6. [How to Connect](#6-how-to-connect)
7. [Sensor Gating Design](#7-sensor-gating-design)

---

## 1. How the CLI Works

The CLI is a classic **interrupt-driven, queue-buffered, task-parsed** terminal â€” a design pattern used throughout professional embedded firmware. Here is the data flow from keypress to hardware response:

```
User types a character in terminal
        â†“
USART1 peripheral receives byte into DR register
        â†“
USART1_IRQHandler fires (RXNE interrupt)
        â†“
Byte extracted from USART1->DR
        â†“
xQueueSendFromISR(xRXQueue, &byte, ...)    â† ISR exits immediately
        â†“
TerminalTask unblocks (was waiting on xRXQueue)
        â†“
Byte appended to line_buf[]
        â†“
If byte == '\r' â†’ line_buf is null-terminated â†’ parse_command()
        â†“
Command dispatched:
  â”œâ”€â”€ "sensor start"  â†’ xQueueSend(xSensorQueue, SENSOR_ARM, 0)
  â”œâ”€â”€ "led cascade"   â†’ xQueueSend(xLEDQueue, LED_CMD_CASCADE, 0)
  â”œâ”€â”€ "status"        â†’ HAL_UART_Transmit(uptime string)
  â””â”€â”€ unknown         â†’ HAL_UART_Transmit("Unknown command\r\n")
        â†“
HAL_UART_Transmit sends response back to terminal
```

### Key Design Properties

| Property | Implementation | Benefit |
|---|---|---|
| Non-blocking receive | USART1 RXNE interrupt â†’ queue | TerminalTask sleeps until data arrives |
| Buffered input | xRXQueue depth = 64 bytes | Handles rapid typing bursts without loss |
| Line-oriented parsing | Assembly ends at `\r` or `\n` | Compatible with all terminal software |
| Single-task parsing | Only TerminalTask calls parse_command() | No concurrent access issues |
| Hardware commands via queues | No direct peripheral access from CLI | Maintains single-owner architecture |

---

## 2. Why Interrupt-Driven UART

### The Alternative: Polling UART

```c
// BAD: polling approach â€” wastes CPU time spinning on USART status
while (1) {
    if (USART1->ISR & USART_ISR_RXNE) {   // check "data ready" flag in a tight loop
        char c = USART1->RDR;              // read received byte
        process(c);
    }
    // While spinning here waiting for a character, NOTHING ELSE CAN RUN
    // SensorTask cannot process echo data
    // HeartbeatTask cannot toggle LED
    // Everything starves
}
```

In a polling design, the CPU is locked in a busy-wait loop checking whether a character has arrived. At 115200 baud, a single byte takes **87Âµs** to arrive. The MCU burns thousands of cycles doing nothing useful.

### Our Approach: Interrupt-Driven

```
User types 'h'
     â”‚
     â–¼
USART1 peripheral finishes receiving the byte
     â”‚
     â–¼ (hardware generates RXNE interrupt â€” takes ~dozen cycles)
USART1_IRQHandler:
     â”œâ”€ Read byte from RDR in one instruction (clears RXNE automatically)
     â”œâ”€ xQueueSendFromISR â†’ posts byte to xRXQueue
     â””â”€ portYIELD_FROM_ISR â†’ TerminalTask unblocks if higher priority
     â”‚
     â–¼ (ISR exits â€” total ISR time < 1Âµs)
CPU resumes whatever it was doing, or switches to TerminalTask
```

**Benefits:**
- **Zero polling overhead**: CPU does nothing between characters
- **No missed bytes**: The interrupt fires within a few cycles of byte receipt; the USART has a 1-byte receive buffer (RDR), and the interrupt clears it immediately
- **FreeRTOS integration**: `xQueueSendFromISR` and `portYIELD_FROM_ISR` allow TerminalTask to run immediately when a character arrives, without waiting for the next scheduler tick
- **Deterministic latency**: Character â†’ ISR â†’ queue â†’ task in <10Âµs end-to-end

---

## 3. The USART1_IRQHandler Walkthrough

```c
void USART1_IRQHandler(void) {
    // FreeRTOS context-switch request flag
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // â”€â”€ Check for RXNE (Receive Data Register Not Empty) interrupt â”€â”€
    // RXNE is set by hardware when a complete byte has been received and
    // loaded into the receive data register (USART1->RDR).
    // Reading RDR automatically clears the RXNE flag.
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {

        // Read the received byte.
        // IMPORTANT: Read RDR exactly once â€” each read clears RXNE and advances the shift register.
        // Reading twice would consume the next byte before it is ready.
        uint8_t received_byte = (uint8_t)(USART1->RDR & 0xFF);
        // The 0xFF mask is defensive â€” RDR is 9 bits wide (for 9-bit UART mode);
        // we're in 8-bit mode so only bits [7:0] are valid anyway.

        // Post byte to the receive queue using ISR-safe API.
        // - xRXQueue has 64 slots â€” if full, this silently drops the byte (pdFAIL returned).
        // - We pass NULL for timeout â€” ISR functions must NEVER block.
        // - xHigherPriorityTaskWoken will be set to pdTRUE if TerminalTask was sleeping
        //   on xQueueReceive and is now ready to run.
        xQueueSendFromISR(xRXQueue, &received_byte, &xHigherPriorityTaskWoken);
    }

    // â”€â”€ Check for UART errors (optional but good practice) â”€â”€
    // ORE = Overrun Error: new byte arrived before RDR was read â€” we lost a byte
    // NE  = Noise Error: line noise caused a framing issue
    // FE  = Framing Error: stop bit not detected where expected
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) ||
        __HAL_UART_GET_FLAG(&huart1, UART_FLAG_NE)  ||
        __HAL_UART_GET_FLAG(&huart1, UART_FLAG_FE))  {
        // Clear all error flags â€” do this by reading SR then RDR
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        __HAL_UART_CLEAR_NEFLAG(&huart1);
        __HAL_UART_CLEAR_FEFLAG(&huart1);
        // In a production system, you would increment an error counter here
    }

    // â”€â”€ Trigger context switch if needed â”€â”€
    // If TerminalTask just became Ready (because we sent it a byte),
    // and TerminalTask has higher priority than the interrupted task,
    // request an immediate context switch via PendSV.
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### How TerminalTask Assembles the Line

```c
void TerminalTask(void *pvParameters) {
    char    line_buf[64]; // command line accumulation buffer
    uint8_t pos = 0;      // current write position in line_buf
    uint8_t byte;         // received byte from queue

    // Print startup banner once
    const char *banner = "\r\n========================================\r\n"
                         "  STM32-FreeRTOS Embedded System v1.0  \r\n"
                         "  Type 'help' for available commands    \r\n"
                         "========================================\r\n> ";
    HAL_UART_Transmit(&huart1, (uint8_t*)banner, strlen(banner), HAL_MAX_DELAY);

    for (;;) {
        // Block indefinitely until a byte arrives in xRXQueue
        // CPU is yielded to lower-priority tasks while waiting
        if (xQueueReceive(xRXQueue, &byte, portMAX_DELAY) == pdTRUE) {

            // Echo the received character back to the terminal (so user sees what they typed)
            HAL_UART_Transmit(&huart1, &byte, 1, 10);

            if (byte == '\r' || byte == '\n') {
                // Line complete â€” null-terminate and dispatch
                line_buf[pos] = '\0';

                if (pos > 0) { // ignore empty lines (just pressing Enter)
                    HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 10);
                    parse_command(line_buf); // dispatch to command handler
                }

                // Reset buffer and print new prompt
                pos = 0;
                HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n> ", 4, 10);

            } else if (byte == 0x7F || byte == '\b') {
                // Backspace: remove last character from buffer
                if (pos > 0) {
                    pos--;
                    // Erase character from terminal display: move back, space, move back
                    HAL_UART_Transmit(&huart1, (uint8_t*)"\b \b", 3, 10);
                }
            } else if (pos < sizeof(line_buf) - 1) {
                // Normal printable character: add to buffer
                line_buf[pos++] = byte;
            }
            // If buffer is full (pos == 63), extra characters are silently discarded
        }
    }
}
```

---

## 4. Command Reference

All commands are case-insensitive. Send with `\r` (Enter key) or `\n`.

| Command | Description | Effect on Hardware | Expected Serial Output |
|---|---|---|---|
| `help` | Lists all available commands | None | Formatted command list with descriptions |
| `sensor start` | Arms the sensing system | Enables HC-SR04 trigger loop, IR monitoring, LED distance bar graph | `[OK] Sensors armed. LED bar graph active.\r\n` then distance readings every 500ms |
| `sensor stop` | Disarms the sensing system | Stops HC-SR04 triggers, turns off all PWM LEDs, clears IR override | `[OK] Sensors stopped. LEDs off.\r\n` |
| `led cascade` | Runs Knight Rider sweep pattern | Sweeps LEDs 1â†’4â†’1 three times at full brightness | `[OK] Running cascade pattern...\r\n` then `Done.\r\n` |
| `led flash` | Blinks all LEDs five times | All four LEDs blink on/off at 100% duty, 5 cycles at 200ms interval | `[OK] Running flash pattern...\r\n` then `Done.\r\n` |
| `status` | Reports system uptime and state | None | `Uptime: 12345 ms | Sensors: ARMED | IR: CLEAR\r\n` |

### Command Implementation: parse_command()

```c
void parse_command(const char *cmd) {
    // Normalize to lower case for case-insensitive matching
    char lower[64];
    for (int i = 0; cmd[i] && i < 63; i++) lower[i] = tolower((unsigned char)cmd[i]);
    lower[strlen(cmd)] = '\0';

    if (strcmp(lower, "help") == 0) {
        // â”€â”€ help â”€â”€
        const char *help_text =
            "Available commands:\r\n"
            "  sensor start  - Arm sensors, enable LED bar graph\r\n"
            "  sensor stop   - Disarm sensors, turn off LEDs\r\n"
            "  led cascade   - Knight Rider sweep pattern (3 passes)\r\n"
            "  led flash     - Blink all LEDs 5x\r\n"
            "  status        - Show uptime and sensor state\r\n"
            "  help          - Show this message\r\n";
        HAL_UART_Transmit(&huart1, (uint8_t*)help_text, strlen(help_text), HAL_MAX_DELAY);

    } else if (strcmp(lower, "sensor start") == 0) {
        // â”€â”€ sensor start â”€â”€
        uint32_t cmd_msg = SENSOR_CMD_ARM;
        if (xQueueSend(xSensorQueue, &cmd_msg, 0) == pdTRUE) {
            HAL_UART_Transmit(&huart1,
                (uint8_t*)"[OK] Sensors armed. LED bar graph active.\r\n", 43, 100);
        }

    } else if (strcmp(lower, "sensor stop") == 0) {
        // â”€â”€ sensor stop â”€â”€
        uint32_t cmd_msg = SENSOR_CMD_DISARM;
        xQueueSend(xSensorQueue, &cmd_msg, 0);
        HAL_UART_Transmit(&huart1,
            (uint8_t*)"[OK] Sensors stopped. LEDs off.\r\n", 33, 100);

    } else if (strcmp(lower, "led cascade") == 0) {
        // â”€â”€ led cascade â”€â”€
        uint32_t cmd_msg = LED_CMD_CASCADE;
        xQueueSend(xLEDQueue, &cmd_msg, 0);
        HAL_UART_Transmit(&huart1,
            (uint8_t*)"[OK] Running cascade pattern...\r\n", 33, 100);

    } else if (strcmp(lower, "led flash") == 0) {
        // â”€â”€ led flash â”€â”€
        uint32_t cmd_msg = LED_CMD_FLASH;
        xQueueSend(xLEDQueue, &cmd_msg, 0);
        HAL_UART_Transmit(&huart1,
            (uint8_t*)"[OK] Running flash pattern...\r\n", 31, 100);

    } else if (strcmp(lower, "status") == 0) {
        // â”€â”€ status â”€â”€
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        char buf[128];
        snprintf(buf, sizeof(buf),
            "Uptime: %lu ms | Sensors: %s | IR: %s\r\n",
            uptime_ms,
            sensors_armed ? "ARMED" : "DISARMED",
            ir_obstacle_present ? "OBSTACLE" : "CLEAR");
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);

    } else {
        // â”€â”€ unknown command â”€â”€
        const char *err = "[ERR] Unknown command. Type 'help'.\r\n";
        HAL_UART_Transmit(&huart1, (uint8_t*)err, strlen(err), 100);
    }
}
```

---

## 5. Example Session

The following is a complete annotated terminal transcript showing a typical operating session. Characters typed by the user are shown; responses are from the firmware.

```
â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
            Terminal Connection Established
            115200 baud | 8N1 | No flow control
â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

(Power-on or reset â€” bootloader runs first, validates CRC, jumps to app)
(FreeRTOS starts, all tasks created, scheduler begins)

========================================
  STM32-FreeRTOS Embedded System v1.0
  Type 'help' for available commands
========================================
>
```
*(The banner prints within ~50ms of reset. The PA5 onboard LED begins blinking at 1Hz â€” HeartbeatTask is running.)*

```
> help
                                            â† user typed "help" + Enter
Available commands:
  sensor start  - Arm sensors, enable LED bar graph
  sensor stop   - Disarm sensors, turn off LEDs
  led cascade   - Knight Rider sweep pattern (3 passes)
  led flash     - Blink all LEDs 5x
  status        - Show uptime and sensor state
  help          - Show this message

>
```

```
> status
                                            â† typed "status" + Enter
Uptime: 4823 ms | Sensors: DISARMED | IR: CLEAR

>
```
*(Sensors are disarmed by default â€” LEDs are off, no readings printed)*

```
> sensor start
                                            â† typed "sensor start" + Enter
[OK] Sensors armed. LED bar graph active.

Distance: 47 cm  [----]                    â† SensorTask begins printing every 500ms
Distance: 46 cm  [----]                    â† no LEDs lit (>30cm threshold)
Distance: 31 cm  [----]
Distance: 28 cm  [L4--]                    â† hand approaching: LED4 (farthest) lights up
Distance: 21 cm  [L4--]
Distance: 17 cm  [L43-]                    â† LED3 joins (15â€“20cm range)
Distance: 13 cm  [L432]                    â† LED2 joins
Distance:  8 cm  [L432]
Distance:  6 cm  [L432]
Distance:  4 cm  [L4321]                   â† LED1 joins: all LEDs at 100% (â‰¤5cm)

```
*(At this point, a different object is waved across the IR sensor)*

```
*** IR OBSTACLE DETECTED ***               â† IR ISR fires, SensorTask posts LED_CMD_IR_ON
[All LEDs blinking at 100% â€” IR override active]

*** IR CLEARED ***                         â† object removed, rising edge on PA7
[Returning to ultrasonic distance mode]

Distance: 35 cm  [----]                    â† hand has moved away
Distance: 40 cm  [----]

>
```

```
> led cascade
                                            â† typed "led cascade" + Enter
[OK] Running cascade pattern...
[LEDs sweep: LED1â†’LED2â†’LED3â†’LED4â†’LED3â†’LED2â†’LED1, repeating 3 times]
Done.

>
```

```
> led flash
[OK] Running flash pattern...
[All four LEDs flash ON/OFF 5 times at 200ms interval]
Done.

>
```

```
> sensor stop
[OK] Sensors stopped. LEDs off.

>
```

```
> unknowncmd
[ERR] Unknown command. Type 'help'.

>
```

```
> status
Uptime: 38291 ms | Sensors: DISARMED | IR: CLEAR

>
```

---
<img width="1913" height="1079" alt="Screenshot 2026-05-31 194029" src="https://github.com/user-attachments/assets/cd2a355b-356f-4cda-bb0b-dddf9cefb264" />

## 6. How to Connect

### Option A: Tera Term (Recommended â€” Used in Development)

Tera Term v5.6.1 is the terminal used during development and for XMODEM OTA updates.

1. Install [Tera Term v5.6.1](https://github.com/TeraTermProject/teraterm/releases)
2. Open Tera Term â†’ **New connection** â†’ Select **Serial**
3. Choose the COM port shown as **STMicroelectronics Virtual COM Port** in Device Manager
4. Go to **Setup â†’ Serial port** and configure:

| Setting | Value |
|---|---|
| Port | Your COM port (e.g., COM3, COM7) |
| Baud rate | **115200** |
| Data bits | **8** |
| Parity | **None** |
| Stop bits | **1** |
| Flow control | **None** |

5. Press **Reset** on the Nucleo board â€” the startup banner should appear immediately

> [!TIP]
> In Tera Term, enable **Local Echo** under **Setup â†’ Terminal** if you want to see characters as you type them. The firmware also echoes received characters back, so you may see double characters without this â€” disable one or the other.

### Option B: PuTTY

1. Open PuTTY â†’ **Serial** connection type
2. Serial line: your COM port
3. Speed: **115200**
4. Go to **Category â†’ Connection â†’ Serial**:

| Setting | Value |
|---|---|
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None |

5. Click **Open**

### Option C: VS Code Serial Monitor (PlatformIO)

1. Build and flash the firmware via PlatformIO
2. In VS Code, open the **PlatformIO Serial Monitor** (plug icon in the status bar)
3. Select your port, set baud to **115200**
4. Line ending: **CR+LF** (or just CR â€” both work)
5. Click **Start Monitoring**

> [!NOTE]
> The PlatformIO Serial Monitor does **not** support XMODEM transfers. For OTA firmware updates, use Tera Term (Option A), which has built-in XMODEM-CRC support.

### Finding the Correct COM Port

On Windows:
- Open **Device Manager** â†’ **Ports (COM & LPT)**
- Look for **STMicroelectronics Virtual COM Port (COMx)**
- This appears when the Nucleo's USB cable is plugged in and the ST-Link driver is installed

```powershell
# PowerShell: list available COM ports
[System.IO.Ports.SerialPort]::getportnames()
```

### Connection Settings Summary

| Parameter | Value |
|---|---|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None (N) |
| Stop bits | 1 |
| Flow control | None (no RTS/CTS, no XON/XOFF) |
| Line ending | CR (`\r`) or CR+LF (`\r\n`) |
| UART pins | PA9 = TX, PA10 = RX |
| Physical interface | USB via ST-Link Virtual COM Port |

---

## 7. Sensor Gating Design

### What Sensor Gating Means

Out of the box, the sensors are **disarmed**. The HC-SR04 does not fire trigger pulses. The IR sensor is monitored but its output does not drive LEDs. No distance readings are printed to the terminal.

You must explicitly type `sensor start` to arm the system.

### Why This Is a Deliberate Design Choice

**1. Prevents accidental activation on boot**

When the board powers on, the LED bar graph would immediately start reacting to whatever happens to be near the sensor â€” a hand hovering while plugging in the USB cable, a monitor sitting within 30cm, a desk surface reflecting stray pulses. This would appear broken to anyone not expecting it.

Requiring an explicit `sensor start` means the system is in a known, stable, dark state at startup. The engineer is in control of when sensing begins.

**2. Allows for calibration before use**

Before arming, you can type `status` to check the system is live (uptime incrementing, Heartbeat blinking). You can test LED patterns with `led cascade` or `led flash` without the sensor loop interfering. Only when you are satisfied does `sensor start` arm everything.

**3. Supports the XMODEM OTA workflow**

During an OTA firmware update (bootloader XMODEM mode), the application is not running. But if the application did start unexpectedly (e.g., if the bootloader jumps to app for a version check), having sensors auto-armed would cause unintended LED behavior and UART output that could confuse the XMODEM protocol parser.

**4. Clean separation of initialization and operation**

This follows a principle common in industrial embedded systems: peripherals are initialized but not activated at startup. Activation requires an explicit command. This makes the startup sequence predictable, debuggable, and safe.

```c
// SensorTask respects the armed/disarmed state
void SensorTask(void *pvParameters) {
    uint8_t armed = 0; // start disarmed
    SensorMessage_t msg;

    for (;;) {
        // Check for arm/disarm commands from TerminalTask
        if (xQueuePeek(xSensorQueue, &msg, 0) == pdTRUE) {
            if (msg.type == SENSOR_CMD) {
                xQueueReceive(xSensorQueue, &msg, 0);
                armed = (msg.value == SENSOR_CMD_ARM) ? 1 : 0;
                if (!armed) {
                    // Disarmed: turn off all LEDs immediately
                    uint32_t off = LED_CMD_ALL_OFF;
                    xQueueSend(xLEDQueue, &off, 0);
                }
            }
        }

        if (armed) {
            // Only fire the trigger and process echoes when armed
            fire_trigger_pulse();
            if (xQueueReceive(xSensorQueue, &msg, pdMS_TO_TICKS(40)) == pdTRUE) {
                if (msg.type == SENSOR_ULTRASONIC) {
                    process_distance(msg.value);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(460)); // remainder of 500ms cycle
        } else {
            vTaskDelay(pdMS_TO_TICKS(100)); // idle: check for arm command every 100ms
        }
    }
}
```

> [!NOTE]
> The IR sensor hardware is always electrically active (it emits IR continuously at the hardware level â€” that's just how the HW-201 module works). Sensor gating in firmware means the ISR-generated events are acknowledged but not forwarded to LEDs when the system is disarmed. The EXTI interrupt fires regardless; only the action taken in SensorTask changes based on the armed state.

---

## Summary

| Aspect | Implementation |
|---|---|
| UART peripheral | USART1, PA9=TX, PA10=RX |
| Baud rate | 115200, 8N1, no flow control |
| Receive method | RXNE interrupt â†’ xRXQueue â†’ TerminalTask |
| Queue | xRXQueue, 64 bytes depth, 1 byte per slot |
| Line parsing | Assembled in TerminalTask on `\r`/`\n` |
| Command dispatch | `parse_command()` in TerminalTask |
| Hardware control | Via xLEDQueue and xSensorQueue â€” no direct peripheral access |
| Sensor activation | Gated â€” requires `sensor start` command, off by default |
| Terminal software | Tera Term v5.6.1 (recommended), PuTTY, VS Code Serial Monitor |

The CLI transforms the embedded system from a silent, monolithic device into an interactive instrument â€” one you can interrogate, command, and observe in real time over a standard serial connection.

---

> **Navigation**: [â† 07 Button & GPIO](07_button_gpio.md) | [Home](../README.md) | [09 Debugging Guide â†’](09_debugging.md)
