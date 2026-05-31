# Troubleshooting Guide

> **The most-googled issues, solved.** Every problem listed here was encountered during development of this project. If your issue is not listed, open a [GitHub Issue](https://github.com/YOUR_USERNAME/STM32-FreeRTOS-Advanced-Embedded-System/issues).

---

## Quick Diagnostic Checklist

Before diving into specific issues, run through this list:

- [ ] Is the Nucleo board's green `LD1` (ST-Link USB LED) solid green?
- [ ] Does PlatformIO detect the board? (`pio device list`)
- [ ] Is the serial terminal set to **115200 baud, 8N1, no flow control**?
- [ ] Is the **SecureBootloader flashed first** before the application?
- [ ] Does `firmware.bin` exist at `.pio/build/nucleo_l476rg/firmware.bin`?

---

## Issue Index

| # | Symptom | Jump To |
|---|---------|---------|
| 1 | Nothing appears on the serial terminal | [→](#1-nothing-appears-on-serial-terminal) |
| 2 | Serial terminal shows garbage / mojibake | [→](#2-serial-terminal-shows-garbage) |
| 3 | Board boots but no `Bootloader Started` message | [→](#3-no-bootloader-message-at-all) |
| 4 | `CRC FAILED! Firmware Corrupted` | [→](#4-crc-failed-firmware-corrupted) |
| 5 | HC-SR04 always reads 4cm | [→](#5-hc-sr04-always-reads-4cm) |
| 6 | HC-SR04 reads wildly incorrect distances | [→](#6-hc-sr04-reads-wildly-incorrect-distances) |
| 7 | LEDs never turn on | [→](#7-leds-never-turn-on) |
| 8 | LEDs turn on but don't fade smoothly | [→](#8-leds-dont-fade-smoothly) |
| 9 | IR sensor doesn't trigger / always triggered | [→](#9-ir-sensor-not-working) |
| 10 | XMODEM transfer stuck at 0.6% (128 bytes) | [→](#10-xmodem-stuck-at-06--128-bytes) |
| 11 | XMODEM transfer starts but fails mid-way | [→](#11-xmodem-fails-mid-transfer) |
| 12 | After XMODEM, board shows `CRC FAILED` | [→](#12-after-xmodem-board-shows-crc-failed) |
| 13 | Board stuck in bootloader (LED blinks fast) | [→](#13-board-stuck-in-bootloader) |
| 14 | `sensor start` typed but no distance readings | [→](#14-sensor-start-but-no-readings) |
| 15 | PlatformIO upload fails with `open failed` | [→](#15-platformio-upload-fails) |

---

## 1. Nothing Appears on Serial Terminal

**Symptoms**: Terminal is open, board is on, complete silence.

**Checks**:
1. **Wrong COM port** — In Device Manager (Windows), look under "Ports (COM & LPT)". The Nucleo appears as `STMicroelectronics STLink Virtual COM Port (COMx)`. Make sure Tera Term / PuTTY is connected to *this* port.
2. **Wrong baud rate** — Must be **115200**. Not 9600, not 115200 with flow control. In Tera Term: Setup → Serial Port → Speed = 115200.
3. **Another application has the port open** — PlatformIO's serial monitor, Arduino IDE, or a previous Tera Term session can hold the COM port. Close all other serial applications.
4. **UART pins not connected** — The Nucleo-L476RG uses the ST-Link's virtual COM port which routes internally to PA2/PA3 for USART2. **This project uses USART1 on PA9/PA10** — if you're using the Nucleo's USB connection for serial, make sure the SB13/SB14 solder bridges route to PA9/PA10, or use an external USB-Serial adapter.

> **Simplest fix**: Connect an external USB-to-TTL serial adapter: adapter's RX → PA9, adapter's TX → PA10, adapter's GND → GND.

---

## 2. Serial Terminal Shows Garbage

**Symptoms**: Characters appear but look like `ÿ¿½`, Chinese characters, or random symbols.

**Cause**: Baud rate mismatch. The UART hardware is receiving bits but at the wrong speed.

**Fix**: Set terminal to exactly **115200 baud**. In Tera Term: Setup → Serial Port → Speed = 115200. Click OK. Press the Nucleo RESET button.

---

## 3. No Bootloader Message at All

**Symptoms**: Board powers on, you see nothing — not even `--- Advanced UART Bootloader Started ---`.

**Possible causes**:
1. **Bootloader not flashed** — Flash `SecureBootloader_L476` first: `cd SecureBootloader_L476 && pio run -t upload`.
2. **Wrong project flashed to wrong address** — Check `platformio.ini`. The bootloader project should NOT have `board_build.flash_offset` set (or it should be `0x0`). The app project should have `board_build.flash_offset = 0x8000`.
3. **UART not connected** — See Issue #1.

---

## 4. CRC FAILED! Firmware Corrupted

**Symptoms**:
```
--- Advanced UART Bootloader Started ---
Magic Number Valid! Verifying CRC...
CRC FAILED! Firmware Corrupted. Entering OTA Mode.
CCCCCCCCCC
```

**Causes and fixes**:

| Cause | Fix |
|-------|-----|
| App flashed without `inject_crc.py` running | Re-flash via `pio run -t upload` (script runs automatically in PlatformIO) |
| App flashed directly via ST-Link debug (not PIO upload) | Always use `pio run -t upload` to ensure post-build script runs |
| Flash got corrupted (partial write) | Re-flash the application |
| Wrong `APPLICATION_ADDRESS` in bootloader | Both bootloader and linker script must agree: `0x08008000` |
| `inject_crc.py` failed silently | Check PlatformIO build output for Python errors; check Python 3 is installed |

---

## 5. HC-SR04 Always Reads 4cm

**This is a known bug we fixed during development.** See the full story in [docs/06_sensors_and_interrupts.md](06_sensors_and_interrupts.md).

**Cause**: The TIM5 prescaler shadow register was not loaded — the timer ran at 80MHz instead of 1MHz, making every echo pulse appear as ~232µs ÷ 58 = 4cm.

**Fix** (already applied in the current code):
```c
TIM5->PSC = 80 - 1;      // Set prescaler
TIM5->EGR = TIM_EGR_UG;  // ← CRITICAL: force load PSC into shadow register
TIM5->CR1 |= TIM_CR1_CEN;
```

If you're still seeing 4cm, verify `TIM5->EGR = TIM_EGR_UG;` is in `MX_TIM5_Init()`.

---

## 6. HC-SR04 Reads Wildly Incorrect Distances

**Symptoms**: Readings jump randomly (30cm, 2cm, 187cm, etc.) with nothing nearby.

**Checks**:
1. **ECHO line floating** — If ECHO pin is not connected, EXTI fires randomly. Verify PB6 is connected to HC-SR04 ECHO.
2. **5V ECHO voltage** — HC-SR04 ECHO outputs 5V. The STM32L476 GPIO is 5V-tolerant on most pins but this can cause noise. Add a voltage divider (10kΩ + 20kΩ) between ECHO and PB6.
3. **Power supply noise** — HC-SR04 draws ~15mA bursts. Use a 100µF decoupling capacitor on the HC-SR04 5V supply rail.
4. **Objects too close** — HC-SR04 minimum range is 2cm. Readings below 2cm are unreliable.
5. **Trigger interval too short** — We fire every 500ms. Do not reduce below 60ms (HC-SR04 needs 60ms to clear its echo).

---

## 7. LEDs Never Turn On

**Symptoms**: `sensor start` typed, distance readings appear, but LEDs stay dark.

**Checks**:
1. **Wrong LED polarity** — LED anode goes to the MCU pin, cathode goes through 220Ω to GND. Reverse will not work.
2. **Resistor missing** — Without a series resistor, the LED draws too much current and the GPIO current-limits or the LED burns out.
3. **Pin not in Alternate Function mode** — The GPIO must be configured as `GPIO_MODE_AF_PP` with the correct `Alternate` number. Check `MX_GPIO_Init()`.
4. **Distance > 30cm** — By design, all LEDs are OFF when the object is further than 30cm. Move your hand closer.
5. **PWM CCR = 0** — If `SetLEDs(0,0,0,0)` was called and nothing triggers an update, LEDs stay off. Type `led flash` to verify PWM hardware is working.

---

## 8. LEDs Don't Fade Smoothly

**Symptoms**: LEDs jump abruptly from off to full brightness with no gradient.

**Cause**: This usually means the timer is configured correctly but the `Period` (ARR) is set wrong — a period of `0xFF` (255) instead of `1000` gives only 256 brightness levels and makes transitions look stepped.

**Fix**: Verify in `MX_PWM_Init()`:
```c
htim1.Init.Period = 1000;  // NOT 255, NOT 0xFF
```

All four timers (TIM1, TIM2, TIM3) must have `Period = 1000`.

---

## 9. IR Sensor Not Working

### Always triggered (LED always blinking even with nothing in front)
1. **Sensitivity potentiometer too sensitive** — Turn the potentiometer on the HW-201 clockwise to reduce sensitivity.
2. **PA7 floating / not pulled up** — Check `GPIO_PULLUP` is set for PA7 in GPIO init. The HW-201 output is open-drain type; it needs a pull-up.
3. **Sunlight interference** — IR sensors are sensitive to ambient IR (especially sunlight). Test indoors away from direct sun.

### Never triggered (no blinking even with hand in front)
1. **IR LED not powered** — Check VCC and GND connections on HW-201.
2. **Wrong output pin** — HW-201 has both `DO` (digital out) and `AO` (analog out). We use `DO` → connect to PA7.
3. **Sensitivity too low** — Turn the potentiometer counter-clockwise to increase sensitivity.

---

## 10. XMODEM Stuck at 0.6% (128 Bytes)

**This is a known bug we fixed during development.** See the full story in [docs/07_ota_xmodem.md](07_ota_xmodem.md).

**Cause**: ARM Cortex-M4 HardFault caused by unaligned 64-bit memory access during flash programming.

**Fix** (already applied in current code):
```c
// WRONG — causes HardFault if packet+3+i is not 8-byte aligned:
uint64_t dw = *(uint64_t*)(&packet[3 + i]);

// CORRECT — memcpy handles any alignment:
uint64_t dw;
memcpy(&dw, &packet[3 + i], 8);
```

If you're still seeing this, verify the `memcpy` fix is in `XMODEM_Receive()` inside `SecureBootloader_L476/src/main.c`.

---

## 11. XMODEM Fails Mid-Transfer

**Symptoms**: Transfer starts, makes progress (10%, 20%, etc.) then stalls or Tera Term shows "XMODEM transfer failed".

**Checks**:
1. **Baud rate mismatch** — XMODEM is very sensitive to timing. Ensure both Tera Term and UART init are at **115200**.
2. **Hardware flow control** — Tera Term must have hardware flow control **disabled**. Setup → Serial Port → Flow control = None.
3. **USB cable quality** — A marginal USB cable can cause bit errors at 115200 baud. Try a different cable.
4. **Retransmit limit exceeded** — XMODEM will NAK and retry a packet up to 10 times before aborting. If you see many retries, check USB-Serial adapter quality.

---

## 12. After XMODEM, Board Shows CRC FAILED

**Symptoms**: XMODEM finishes at 100%, board reboots, but bootloader shows `CRC FAILED`.

**Cause**: The `firmware.bin` file was NOT processed by `inject_crc.py` before the XMODEM transfer.

**Fix**:
1. Build the application in PlatformIO: `pio run` (this automatically runs `inject_crc.py`)
2. Check build output for `[SUCCESS] Checksum and Length successfully injected into firmware.bin!`
3. Only THEN send this `.pio/build/nucleo_l476rg/firmware.bin` via XMODEM

> **Never manually copy or rename** the `.bin` file — `inject_crc.py` patches specific byte offsets, so any modification after injection will break the CRC.

---

## 13. Board Stuck in Bootloader (LED Blinks Fast)

**Symptoms**: PB4 LED blinks rapidly at ~5Hz. Nothing launches on the serial terminal beyond the bootloader message.

**Cause**: Either the application is not flashed, or the application CRC check failed, and the bootloader entered OTA mode automatically.

**Fix**:
1. Connect Tera Term at 115200 baud
2. You should see `CCCCCC...` — this means the bootloader is waiting for XMODEM
3. Send the application via XMODEM (File → Transfer → XMODEM → Send → `firmware.bin`)
4. OR: Flash the application via ST-Link: `cd FreeRTOS_App_L476 && pio run -t upload`

---

## 14. `sensor start` Typed But No Distance Readings

**Symptoms**: CLI accepts the command (`[CLI] Starting Real-Time Sensors...`) but no `[HC-SR04] Distance:` lines appear.

**Checks**:
1. **TRIG wire not connected** — PC7 must be connected to HC-SR04 TRIG. Without TRIG, the sensor never fires.
2. **HC-SR04 not powered** — HC-SR04 requires **5V** (not 3.3V). Connect VCC to the Nucleo's 5V pin.
3. **ECHO wire not connected** — PB6 must be connected to HC-SR04 ECHO. Without ECHO, the EXTI never fires, the ISR never runs, and nothing is ever queued.
4. **Object out of range** — HC-SR04 range is 2cm–400cm. No reading appears if nothing is within range (the echo times out). We filter readings outside this range: `if (dist > 0 && dist < 400)`.

---

## 15. PlatformIO Upload Fails

### `Error: open failed` from OpenOCD
This means the ST-Link could not be accessed. Causes:
1. **USB cable disconnected or wrong port** — Check the cable goes to the Nucleo's USB (not a separate hub)
2. **ST-Link firmware needs update** — Download [ST-Link Utility](https://www.st.com/en/development-tools/stsw-link004.html) and update firmware
3. **Another process has the debugger** — Close any running debug sessions in VS Code or STM32CubeIDE

### `Error: Error flashing to target`
The board is in a bad state. Try:
```bash
# Power cycle the board, then:
pio run -t upload
```
If that fails, hold the Nucleo RESET button, release it, immediately run `pio run -t upload`.

---

## Still Stuck?

1. Read the relevant deep-dive doc in [`docs/`](00_index.md)
2. Open a [GitHub Issue](https://github.com/YOUR_USERNAME/STM32-FreeRTOS-Advanced-Embedded-System/issues/new/choose) using the Bug Report template — include your serial terminal output!

---

*← [Performance Analysis](09_performance_analysis.md)* | *[Back to docs index](00_index.md)* | *[Back to main README](../README.md)*
