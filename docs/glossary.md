# Glossary

> Every technical term used in this repository, explained in plain language.
> If you encounter a word you don't know while reading the docs, look it up here first.

---

## A

**ARM Cortex-M4**
The CPU inside the STM32L476RG. It is a 32-bit processor designed for microcontrollers. The "M4" variant includes a floating-point unit (FPU) and DSP instructions, making it well-suited for signal processing and sensor work. It runs at up to 80 MHz on this board.

**ARR (Auto-Reload Register)**
A hardware register inside an STM32 timer. The timer counts up from 0 to the ARR value, then resets to 0 and repeats. Setting ARR controls the PWM period. Like PSC, it is shadow-buffered and requires a forced update event to take effect immediately.

---

## B

**Bare-metal**
Writing firmware that runs directly on the hardware with no operating system. You control every register manually. This project contains one bare-metal component (the bootloader) and one RTOS component (the FreeRTOS application).

**Bootloader**
A small program that runs before your main application on every power-on. Its job is to verify the application is safe to run (using CRC), and either launch it or enter a recovery mode to receive new firmware. The bootloader in this project lives at Flash address `0x08000000`.

**Bus Fault**
A type of HardFault on ARM Cortex-M processors. It occurs when the CPU tries to access memory in a way the hardware does not support: for example, reading a 64-bit value from an address that is not 8-byte aligned. See the XMODEM unaligned access bug in the README for a real example.

---

## C

**CCR (Capture/Compare Register)**
A hardware register inside an STM32 timer. For PWM output, the CCR value sets the duty cycle: the timer output stays HIGH while the counter is below CCR and goes LOW when the counter exceeds CCR. Higher CCR = longer HIGH = brighter LED (for active-high PWM).

**CLI (Command-Line Interface)**
A text-based interface where you type commands and get text responses. In this project, the CLI runs over UART: you connect a serial terminal (like Tera Term) and type `sensor start`, `led flash`, `status`, etc.

**CRC32 (Cyclic Redundancy Check, 32-bit)**
A mathematical algorithm that produces a 4-byte checksum from any block of data. If the data changes even by 1 bit, the CRC value changes. Used in this project to verify that the firmware stored in Flash has not been corrupted before booting into it.

**Cortex-M4**
See *ARM Cortex-M4*.

---

## D

**Double-word**
8 bytes (64 bits). The STM32L476 Flash memory can only be programmed in double-word chunks: you cannot write fewer than 8 bytes at a time to Flash. This is why the XMODEM receiver reads the payload 8 bytes at a time before calling `HAL_FLASH_Program`.

**Duty Cycle**
In PWM, the percentage of time the signal is HIGH within one period. 0% = always LOW (LED off). 100% = always HIGH (LED fully on). 50% = half-on, which makes the LED appear at half brightness.

---

## E

**EGR (Event Generation Register)**
A write-only register in STM32 timers. Writing `TIM_EGR_UG` (bit 0) to this register forces an immediate Update Event, which causes shadow registers (PSC, ARR) to be copied into their active counterparts right now. Essential after changing timer parameters before the timer has overflowed naturally.

**EXTI (External Interrupt/Event Controller)**
The STM32 peripheral that monitors GPIO pins for signal edges (rising, falling, or both) and fires an interrupt when detected. In this project, EXTI watches PB6 (HC-SR04 ECHO), PA7 (HW-201 IR OUT), and PC13 (Blue button).

**EXTI9_5_IRQHandler**
A single interrupt handler on STM32 that fires for any of EXTI lines 5 through 9. Because both PB6 (line 6) and PA7 (line 7) share this handler, the ISR must check which line fired by reading the pending register before acting.

---

## F

**Flash Memory**
Non-volatile storage built into the STM32 chip. Unlike RAM, it keeps its contents when power is removed. The STM32L476RG has 1 MB of Flash. The bootloader occupies the first 32 KB (`0x08000000`-`0x08007FFF`), and the application occupies the rest starting at `0x08008000`.

**FreeRTOS**
A free, open-source real-time operating system kernel for microcontrollers. It provides tasks (like threads), queues (for passing data between tasks), semaphores, and a scheduler that switches between tasks. It does not run on a computer: it runs inside your MCU.

---

## G

**GPIO (General Purpose Input/Output)**
A digital pin on the MCU that you can configure as either an input (to read HIGH/LOW from external circuitry) or an output (to drive HIGH/LOW to external circuitry). Most MCU pins are GPIO by default and can be reconfigured to serve a specific peripheral (like a timer or UART).

---

## H

**HAL (Hardware Abstraction Layer)**
A library provided by STMicroelectronics that wraps all the register-level operations behind simple function calls. `HAL_GPIO_WritePin`, `HAL_FLASH_Program`, `HAL_UART_Transmit`: these all call HAL functions. Using HAL means you don't have to memorise every register name, but you still need to understand what they do.

**HardFault**
The ARM Cortex-M "catch-all" fault exception. When something goes seriously wrong: illegal memory access, unaligned access on a multi-word operation, executing invalid instructions: the processor jumps to the HardFault handler. If no handler is defined, the MCU locks up. See the XMODEM bug in the README for a real HardFault example.

**HC-SR04**
An ultrasonic distance sensor. It sends a burst of 40kHz sound pulses and measures the time until the echo returns. The distance is calculated from the echo pulse duration. In this project it is connected to PC7 (TRIG) and PB6 (ECHO via voltage divider).

**HW-201**
An infrared proximity sensor module. It emits modulated IR light and detects reflections. Its output (OUT/DO pin) goes LOW when an object is detected (active-LOW logic). Connected to PA7 in this project.

---

## I

**ISR (Interrupt Service Routine)**
A function that runs automatically when a hardware event occurs, interrupting whatever the CPU was doing at that moment. ISRs must be fast: they should read/write registers and post to queues, never block or call slow functions. In FreeRTOS, ISRs use `FromISR` suffixed API functions (e.g., `xQueueSendFromISR`).

---

## L

**Linker Script**
A file (`.ld`) that tells the compiler/linker where to place code and data in memory. The bootloader's linker script places code at `0x08000000`. The application's linker script places code at `0x08008000`. Without the correct linker script, firmware flashed at the wrong address will not boot.

---

## M

**MCU (Microcontroller Unit)**
A single chip that contains a CPU, RAM, Flash memory, and peripheral controllers (timers, UART, SPI, etc.) all on one piece of silicon. The STM32L476RG is the MCU in this project.

**MSP (Main Stack Pointer)**
A register in ARM Cortex-M that holds the current top of the main stack. When the bootloader jumps to the application, it must manually set the MSP to the value stored at the start of the application's vector table: otherwise the application starts using the bootloader's stack memory, causing corruption.

---

## N

**NVIC (Nested Vectored Interrupt Controller)**
The ARM hardware block that manages all interrupts: their priorities, their enable/disable state, and their dispatch to the correct ISR. In FreeRTOS, NVIC priorities must be configured carefully: ISRs that call `FromISR` API functions must have a priority number greater than or equal to `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`.

---

## O

**OTA (Over-The-Air)**
Updating firmware wirelessly or remotely without physical access to the debug port. In this project "OTA" means Over-UART: new firmware is sent over the serial cable using the XMODEM protocol, and the bootloader writes it to Flash. No ST-Link or physical debugger is needed.

---

## P

**PlatformIO**
A VS Code extension and build system for embedded development. It manages toolchains, libraries, upload tools, and serial monitors in one place. Used in this project instead of STM32CubeIDE. Each project needs its own `platformio.ini` configuration file.

**portYIELD_FROM_ISR**
A FreeRTOS macro called at the end of an ISR. If an ISR woke a higher-priority task (by sending to a queue), this macro triggers an immediate context switch after the ISR returns: so the high-priority task runs right away instead of waiting for the next scheduler tick.

**PSC (Prescaler Register)**
A hardware register in STM32 timers that divides the input clock before it reaches the counter. If the system clock is 80 MHz and PSC = 79, the timer counts at 80 MHz / (79+1) = 1 MHz. PSC is shadow-buffered: see *Shadow Register*.

**PWM (Pulse Width Modulation)**
A technique for simulating analog output using a digital signal. The signal rapidly switches between HIGH and LOW. The fraction of time it spends HIGH (the duty cycle) determines the effective output level. Used in this project to control LED brightness via hardware timer channels.

---

## Q

**Queue (FreeRTOS)**
A thread-safe FIFO buffer for passing data between tasks or between ISRs and tasks. A task can block (sleep) while waiting for data to arrive in a queue, allowing the scheduler to run other tasks in the meantime. This is how the sensor ISR hands data to SensorTask, and how SensorTask hands LED commands to LEDControllerTask.

---

## R

**RTOS (Real-Time Operating System)**
An operating system designed for embedded systems where timing guarantees matter. Unlike a desktop OS, an RTOS schedules tasks based on priority and can switch between them in microseconds. FreeRTOS is the RTOS used in this project.

---

## S

**SCB (System Control Block)**
A set of ARM Cortex-M registers that control core processor behavior. `SCB->VTOR` sets the vector table address. `SCB->CFSR` (Configurable Fault Status Register) stores the reason for a HardFault: extremely useful for debugging crashes.

**Shadow Register**
A two-register mechanism used by STM32 timers for PSC and ARR. The value you write goes into a "preload" (shadow) register. The actual hardware uses a separate "active" register. The active register is updated from the preload only at an Update Event. This prevents glitches when you change timer parameters mid-period: but it also means your writes are not immediately effective.

**ST-Link**
The on-board debugger chip on the Nucleo board. It connects to your PC via USB and lets you flash firmware, run GDB debugging sessions, and access a Virtual COM Port (for serial communication). After the bootloader is installed, the ST-Link is only needed for bootloader updates: application updates can use OTA.

**SysTick**
A 24-bit countdown timer built into every ARM Cortex-M processor. FreeRTOS uses SysTick to generate its periodic scheduler tick (1ms in this project). Every SysTick interrupt calls `xPortSysTickHandler`, which the scheduler uses to decide if a context switch is needed.

---

## T

**Task (FreeRTOS)**
The FreeRTOS equivalent of a thread. Each task has its own stack, its own priority, and runs as if it were the only code running. The FreeRTOS scheduler switches between tasks rapidly, creating the illusion of parallelism on a single-core CPU.

---

## U

**UART (Universal Asynchronous Receiver-Transmitter)**
A serial communication protocol. Data is sent one bit at a time over a single wire (TX for transmit, RX for receive) at a fixed baud rate. In this project, UART1 at 115200 baud is used for the serial CLI and for XMODEM OTA firmware transfers.

---

## V

**Vector Table**
A table at the start of Flash memory containing addresses of all interrupt handler functions. When an interrupt fires, the CPU reads the handler address from this table and jumps to it. The bootloader's vector table is at `0x08000000`. The application's vector table is at `0x08008000`. The application must tell the CPU to use its own table by writing `SCB->VTOR = 0x08008000`.

**VTOR (Vector Table Offset Register)**
The ARM register (`SCB->VTOR`) that tells the CPU where the interrupt vector table is located. The bootloader must set this to `0x08008000` before jumping to the application, otherwise all interrupts will dispatch to the bootloader's (now inactive) handlers.

---

## X

**XMODEM**
A simple file transfer protocol from 1977 that still works perfectly for embedded OTA updates. It sends data in 128-byte blocks, each verified with a checksum (standard) or CRC16 (XMODEM-CRC variant used here). The receiver sends `C` characters to invite transmission. If a block fails CRC, the receiver sends NAK and the sender retransmits.

---

*If you find a term used in the docs that is not defined here, please open a GitHub Issue and we will add it.*
