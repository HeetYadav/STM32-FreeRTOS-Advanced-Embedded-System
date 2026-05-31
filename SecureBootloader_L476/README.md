# SecureBootloader_L476

> **A custom, minimal Secure Bootloader for the STM32L476RG**: verifies application firmware integrity via CRC32 and supports UART Over-The-Air (OTA) updates using the XMODEM-CRC protocol.

---

## What This Does

On every power-on or reset, this bootloader runs **before** your main application. It performs three jobs:

1. **Integrity Check**: Reads the application's `AppHeader` struct from Flash, verifies a CRC32 checksum using the STM32's hardware CRC peripheral, and only launches the app if the checksum matches.
2. **Secure Launch**: If CRC passes, cleanly tears down all peripherals, relocates the interrupt vector table (`VTOR`), and jumps to the application's `Reset_Handler`.
3. **OTA Update**: If the breadboard button (`PB10`) is held during reset, or if CRC verification fails, the bootloader enters XMODEM-CRC receive mode and programs incoming firmware directly into application flash.

For the full deep-dive, see [`docs/04_secure_bootloader.md`](../docs/04_secure_bootloader.md).

---

## Flash Memory Layout

```
STM32L476RG Flash (1MB)
 0x0800_0000
                                  
   SecureBootloader_L476           Pages 0:15 (32KB)
   (this project)                 
                                  
 0x0800_8000
   FreeRTOS_App_L476              
   (vector table, AppHeader,       Pages 16:511 (~992KB)
    application code)             
                                  
 0x080F_FFFF
```

---

## Hardware Required

| Signal | MCU Pin | Notes |
|--------|---------|-------|
| UART TX (serial output) | PA9 | Connect to USB-Serial adapter RX |
| UART RX (serial input) | PA10 | Connect to USB-Serial adapter TX |
| Bootloader status LED | PB4 | ON during bootloader, blinks when waiting for XMODEM |
| Force-update button | PB10 | Hold during reset to enter OTA mode |

**Serial settings**: 115200 baud, 8 data bits, no parity, 1 stop bit (8N1)

---

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) installed in VS Code
- STM32 Nucleo-L476RG connected via USB

### Build

```bash
# From the SecureBootloader_L476/ directory
pio run
```

### Flash (via ST-Link: one time only)

```bash
pio run -t upload
```

> **Note**: The bootloader must be flashed via ST-Link at least once. After that, the **application** can be updated via XMODEM OTA without the ST-Link.

---

## What You'll See on the Serial Terminal

### Normal Boot (valid application present)

```
--- Advanced UART Bootloader Started ---
Magic Number Valid! Verifying CRC...
CRC MATCH! Jumping to Application...
```

Followed immediately by the FreeRTOS application startup message.

### OTA Update Mode (PB10 held, or CRC failed)

<img width="1919" height="1079" alt="Screenshot 2026-05-31 194216" src="https://github.com/user-attachments/assets/05f6f78a-6c7a-4950-a788-aaffb597e5d6" />

The `C` characters are the XMODEM-CRC initiation signal. Open Tera Term  File  Transfer  XMODEM  Send  select `FreeRTOS_App_L476/.pio/build/nucleo_l476rg/firmware.bin`.

### After Successful OTA

```
[OTA] Update Successfully Flashed! Rebooting...

--- Advanced UART Bootloader Started ---
Magic Number Valid! Verifying CRC...
CRC MATCH! Jumping to Application...
```

---


## Key Source Files

| File | Description |
|------|-------------|
| [`src/main.c`](src/main.c) | All bootloader logic: CRC verification, XMODEM receiver, flash programming, jump mechanism |
| [`platformio.ini`](platformio.ini) | Build configuration: flash offset 0x0000, ST-Link upload |

---

* [Back to main README](../README.md)* | *Next: [FreeRTOS Application ](../FreeRTOS_App_L476/README.md)*
