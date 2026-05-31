# Sensors and Interrupts: HC-SR04, HW-201, and the EXTI Architecture

> **Navigation**: [â† 05 OTA Update System](05_ota_update.md) | [Home](../README.md) | [07 Button & GPIO â†’](07_button_gpio.md)

---

## Table of Contents

1. [HC-SR04 Physics: How Ultrasonic Ranging Works](#1-hc-sr04-physics-how-ultrasonic-ranging-works)
2. [TIM5 as a 1Âµs Stopwatch](#2-tim5-as-a-1Âµs-stopwatch)
3. [Bug Story: The Shadow Register That Broke Everything](#3-bug-story-the-shadow-register-that-broke-everything)
4. [EXTI Architecture on STM32](#4-exti-architecture-on-stm32)
5. [HC-SR04 ISR Walkthrough](#5-hc-sr04-isr-walkthrough)
6. [HW-201 IR Proximity Sensor](#6-hw-201-ir-proximity-sensor)
7. [IR ISR Walkthrough](#7-ir-isr-walkthrough)
8. [Timing Diagram](#8-timing-diagram)

---

## 1. HC-SR04 Physics: How Ultrasonic Ranging Works

The HC-SR04 is a self-contained ultrasonic ranging module. Understanding its physics is essential to understanding every design decision in this section.

### The Measurement Principle

Sound travels through air at approximately **343 m/s** at room temperature (20Â°C). The HC-SR04 exploits this fact:

1. The MCU sends a short trigger pulse to the sensor's TRIG pin
2. The sensor fires a burst of **eight 40kHz ultrasonic pulses** from its transmitter
3. The pulses travel through air, hit an obstacle, and reflect back
4. The sensor's receiver detects the echo and asserts its ECHO pin HIGH
5. When the echo is received, ECHO goes LOW
6. The **duration of the ECHO HIGH pulse** = time-of-flight for the round trip

### The Distance Formula

```
Round-trip time = T_echo (Âµs)
One-way distance = (343 m/s Ã— T_echo Ã— 10â»â¶) / 2
                 = T_echo Ã— 0.0001715 m
                 = T_echo / 58.3 cm

Simplified:   distance_cm = T_echo_Âµs / 58
```

The constant **58** is the standard HC-SR04 divisor â€” it accounts for the two-way trip and the speed of sound.

### Sensor Specifications

| Parameter | Value |
|---|---|
| Supply voltage | 5V DC (tolerant of 3.3V logic from MCU) |
| Trigger pulse | 10Âµs minimum HIGH pulse on TRIG |
| Ultrasonic frequency | 40kHz (inaudible) |
| Burst size | 8 pulses |
| Echo pulse width | 150Âµs â€“ 25ms (proportional to distance) |
| Minimum range | ~2 cm |
| Maximum range | ~400 cm (4m) |
| Blind spot at echo timeout | 38ms = no echo = out of range |

### Our Hardware Connections

| Signal | MCU Pin | Function |
|---|---|---|
| TRIG | PC7 | Output â€” MCU sends 10Âµs pulse here |
| ECHO | PB6 | Input â€” MCU measures this pulse width |

> ðŸ“¸ **[Hardware Photo: HC-SR04 wired to Nucleo-L476RG breadboard, showing TRIG/ECHO connections]** *(Contribute one via PR!)*

### Our Distance-to-LED Mapping

| Distance | LED Behavior |
|---|---|
| > 30 cm | All LEDs OFF |
| 30 cm â†’ 20 cm | LED4 (PA8, farthest) fades in |
| 20 cm â†’ 15 cm | LED3 (PB3) fades in |
| 15 cm â†’ 10 cm | LED2 (PB5) fades in |
| 10 cm â†’ 5 cm | LED1 (PB4, nearest) fades in |
| â‰¤ 5 cm | All LEDs at 100% |

---

## 2. TIM5 as a 1Âµs Stopwatch

### Why We Need Microsecond Resolution

The HC-SR04 echo pulse at 2cm (minimum range) is:
```
T_echo = 2cm Ã— 58 = 116Âµs
```

At 5cm (our nearest threshold):
```
T_echo = 5cm Ã— 58 = 290Âµs
```

We need to measure these pulse widths to within a few microseconds. The SysTick timer runs at 1ms resolution â€” far too coarse. We need a dedicated hardware timer running at **1MHz** (1Âµs per tick).

### Why TIM5 Specifically?

The STM32L476RG has five general-purpose timers. The choice matters:

| Timer | Bit Width | Available for Stopwatch? | Reason |
|---|---|---|---|
| TIM1 | 16-bit | âŒ | Used for LED4 PWM (PA8, TIM1_CH1) |
| TIM2 | 32-bit | âŒ | Used for LED3 PWM (PB3, TIM2_CH2) â€” see TIM2 conflict story |
| TIM3 | 16-bit | âŒ | Used for LED1/LED2 PWM (PB4/PB5, TIM3_CH1/CH2) |
| TIM4 | 16-bit | âš ï¸ | Available but 16-bit only |
| TIM5 | **32-bit** | âœ… | No pin conflicts, 32-bit counter |

**TIM5 wins for two reasons:**

1. **32-bit counter**: At 1MHz, a 16-bit timer overflows after 65.535ms. The HC-SR04 timeout is 38ms â€” dangerously close. A 32-bit timer at 1MHz overflows after 4294 seconds. No overflow risk whatsoever.

2. **No pin conflicts**: TIM5 has no channel pins that overlap with our PWM LED assignments.

### Configuring TIM5 for 1MHz (PSC=79)

The STM32L476RG runs at 80MHz system clock. To get a 1MHz timer:

```
Timer frequency = System clock / (PSC + 1)
1,000,000 Hz    = 80,000,000 Hz / (PSC + 1)
PSC + 1         = 80
PSC             = 79
```

```c
// TIM5 initialization for 1Âµs stopwatch (80MHz system clock)
TIM5->PSC = 79;                     // prescaler: 80MHz / 80 = 1MHz
TIM5->ARR = 0xFFFFFFFF;             // auto-reload: max value (32-bit, never overflow)
TIM5->CR1 &= ~TIM_CR1_CEN;         // start with timer disabled (we enable on echo rising edge)
TIM5->EGR  = TIM_EGR_UG;           // â† CRITICAL: force-load shadow registers NOW
                                    //   Without this line, PSC is buffered and ignored
                                    //   until the next update event â€” which never comes
                                    //   if we enable the timer before the first overflow
TIM5->SR   = 0;                     // clear any pending flags set by EGR write above
```

The `TIM5->EGR = TIM_EGR_UG` line is the most important initialization step and the subject of our most instructive bug. See the next section.

### Using TIM5 as a Stopwatch

```c
// On echo rising edge (ISR):
TIM5->CNT = 0;              // reset counter to 0
TIM5->CR1 |= TIM_CR1_CEN;  // start counting (at 1Âµs per tick)

// On echo falling edge (ISR):
TIM5->CR1 &= ~TIM_CR1_CEN; // stop counting â€” freeze the value
uint32_t elapsed_us = TIM5->CNT; // read elapsed microseconds
```

---

## 3. Bug Story: The Shadow Register That Broke Everything

> *Symptom â†’ Investigation â†’ Root Cause â†’ Fix â†’ Lesson Learned*

### Symptom

After wiring the HC-SR04 and writing the initial ISR code, distance readings were consistently reported as **4 cm** regardless of the actual distance to any obstacle. Moving a hand from 50cm to touching the sensor made no difference â€” always 4cm. The LED bar graph showed LED1 (nearest) always lit, never anything else.

### Investigation

The first suspicion was wiring. TRIG (PC7) and ECHO (PB6) were verified against the schematic. An oscilloscope confirmed the ECHO pin was responding correctly â€” the pulse width visibly changed as a hand moved closer and farther. The hardware was fine.

Next, the TIM5 counter was inspected. The CNT register was read immediately after the echo falling edge inside the ISR and transmitted over UART. The reported value was always approximately **232**, regardless of actual distance.

```
232 Âµs / 58 = 4.0 cm   â† matches the symptom exactly
```

So TIM5 was consistently reading 232Âµs, period. The counter was clearly not counting correctly â€” or not at all.

A logic analyzer showed the ECHO pulse was actually around **1,740Âµs** (about 30cm). TIM5 was counting far too few ticks.

### Root Cause

The STM32 timer **prescaler register (PSC)** is a **shadow register**. This is documented in the STM32 Reference Manual but easy to overlook:

> *"The prescaler is loaded at each update event."*

On ARM STM32 timers, the PSC register has two halves:
- The **preload register** â€” the value you write to `TIM5->PSC`
- The **shadow (active) register** â€” the value actually used by the counter hardware

Writing `TIM5->PSC = 79` updates only the **preload** register. The shadow register retains its previous value (0 after reset â€” meaning the prescaler is 1, not 80, so the timer runs at **80MHz** not 1MHz).

The shadow register is updated automatically at the next **Update Event (UEV)** â€” which normally occurs when the counter overflows from ARR back to 0. But:

1. We disabled the counter (`CR1 &= ~TIM_CR1_CEN`) before first use
2. We enabled it for each echo measurement and stopped it immediately after
3. The counter never overflowed
4. The UEV never fired
5. The PSC shadow register was **never loaded with 79**

So TIM5 ran at 80MHz (PSC=0) instead of 1MHz (PSC=79). Each "tick" was 12.5ns instead of 1Âµs.

```
Actual echo duration: 1,740Âµs = 1,740,000ns
At 80MHz (12.5ns/tick): 1,740,000ns / 12.5ns = 139,200 ticks
But the 16-bit portion overflows at 65,535...

Wait â€” TIM5 is 32-bit. 139,200 fits fine.
But our ISR was stopping TIM5 early...

Re-checking: echo pulse was 2,900ns (2.9Âµs) at 4cm range reading.
At 80MHz: 2,900ns / 12.5ns = 232 ticks â† matches the symptom!
```

The sensor was actually firing back an echo at ~4cm... but that was the **deadband distance** where the ultrasonic burst itself was being received by the microphone before the beam hit any external obstacle. The timer was too fast, reading the wrong short reflection.

### Fix

Force an **Update Generation** event immediately after writing PSC. This immediately copies the preload register into the shadow register:

```c
TIM5->PSC = 79;
TIM5->ARR = 0xFFFFFFFF;
TIM5->EGR = TIM_EGR_UG;  // â† THE FIX: force-load PSC shadow register right now
TIM5->SR  = 0;            // clear UIF flag set by EGR (prevents false update interrupt)
```

After this fix, readings immediately reflected actual distance. Moving a hand from 50cm away showed readings smoothly changing from ~50cm down to 5cm.

### Lesson Learned

> **Always write `TIMx->EGR = TIM_EGR_UG` after modifying PSC or ARR on any STM32 timer before first use.**

This applies to every timer peripheral on the STM32 family. The shadow register mechanism exists to allow PSC and ARR changes to take effect synchronously at the next update event (preventing glitches mid-period) â€” but it means your initialization writes are **not immediately active** unless you force the update event manually.

```c
// The safe timer initialization pattern:
TIMx->CR1  = 0;                  // 1. Disable timer first
TIMx->PSC  = desired_prescaler;  // 2. Write preload register
TIMx->ARR  = desired_period;     // 3. Write auto-reload preload register
TIMx->EGR  = TIM_EGR_UG;        // 4. FORCE shadow register update NOW â† never skip this
TIMx->SR   = 0;                  // 5. Clear UIF flag generated by step 4
TIMx->CR1 |= TIM_CR1_CEN;       // 6. Start timer (now running with correct prescaler)
```

---

## 4. EXTI Architecture on STM32

### How GPIO Pins Map to EXTI Lines

The STM32L476 has 16 external interrupt lines (EXTI0â€“EXTI15). Each EXTI line corresponds to a **pin number** (not a full GPIO address). All GPIOx_Pn pins (PA0, PB0, PC0, etc.) share EXTI line 0. All GPIOx_P6 pins share EXTI line 6.

```
EXTI Line 0  â† PA0, PB0, PC0, PD0 ... (one active at a time, selected by SYSCFG)
EXTI Line 1  â† PA1, PB1, PC1, PD1 ...
...
EXTI Line 6  â† PA6, PB6, PC6, PD6 ...  â† PB6 (HC-SR04 ECHO) uses this line
EXTI Line 7  â† PA7, PB7, PC7, PD7 ...  â† PA7 (IR sensor OUT) uses this line
...
EXTI Line 13 â† PA13, PB13, PC13 ...    â† PC13 (Blue button) uses this line
```

The `SYSCFG_EXTICRx` registers select which GPIO port (PA, PB, PC...) drives each EXTI line.

### EXTI9_5: One Handler for Five Lines

The ARM NVIC has limited interrupt vectors. The STM32L476 combines EXTI lines 5â€“9 into a single IRQ: **EXTI9_5_IRQn**. This means one interrupt handler (`EXTI9_5_IRQHandler`) must check the pending register to determine which line(s) fired.

In our project, this handler serves:
- **PB6** â†’ EXTI line 6 â†’ HC-SR04 echo rising/falling edge
- **PA7** â†’ EXTI line 7 â†’ IR sensor object detect/clear

Both can fire simultaneously (a rising edge on PB6 and a falling edge on PA7 could occur within the same microsecond). The handler must check and clear both.

### Edge Configuration

| Signal | Edge Config | Reason |
|---|---|---|
| PB6 (HC-SR04 ECHO) | Both rising AND falling | Rising = start stopwatch, Falling = stop and read |
| PA7 (IR sensor OUT) | Both rising AND falling | Falling = object detected (active LOW asserted), Rising = object removed |

```c
// EXTI configuration for PB6 (both edges)
GPIO_InitStruct.Pin  = GPIO_PIN_6;
GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING; // both edges
GPIO_InitStruct.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

// EXTI configuration for PA7 (both edges)
GPIO_InitStruct.Pin  = GPIO_PIN_7;
GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING; // both edges
GPIO_InitStruct.Pull = GPIO_PULLUP;                  // pull-up: idle = HIGH (no obstacle)
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

// Enable both in NVIC at the same priority
HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0); // priority 5: below FreeRTOS syscall threshold
HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
```

> [!IMPORTANT]
> FreeRTOS on STM32 requires that any ISR which calls `FromISR` API functions must have its NVIC priority numerically **greater than or equal to** `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (typically set to 5). Lower number = higher hardware priority. We use priority 5, which allows `xQueueSendFromISR` to be called safely.

---

## 5. HC-SR04 ISR Walkthrough

The complete EXTI handler, annotated line by line:

```c
void EXTI9_5_IRQHandler(void) {
    // FreeRTOS: this flag will be set to pdTRUE if our queue send wakes a higher-priority task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Section 1: HC-SR04 Echo on PB6 (EXTI line 6)
    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_6)) {
        // Check which edge fired by reading the current pin state
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) {
            // â”€â”€ RISING EDGE: Echo signal just went HIGH â”€â”€
            // The sensor has fired its 8-pulse burst and is now listening.
            // Start our 1Âµs stopwatch from zero.
            TIM5->CNT = 0;              // reset counter to 0 â€” starts measuring now
            TIM5->CR1 |= TIM_CR1_CEN;  // enable TIM5 (begins counting at 1MHz)
            // No queue post on rising edge â€” we only have start time, not duration yet
        } else {
            // â”€â”€ FALLING EDGE: Echo signal just went LOW â”€â”€
            // The reflected pulse has been received. Stop the stopwatch.
            TIM5->CR1 &= ~TIM_CR1_CEN; // stop TIM5 â€” freeze the count immediately

            // Build a sensor message with the elapsed time
            SensorMessage_t msg;
            msg.type  = SENSOR_ULTRASONIC;
            msg.value = TIM5->CNT;      // CNT ticks = elapsed microseconds (TIM5 at 1MHz)
                                        // distance_cm = msg.value / 58 (done in SensorTask)

            // Post to xSensorQueue using ISR-safe API
            // If SensorTask was blocked waiting on this queue, it becomes Ready immediately
            xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
        }

        // MANDATORY: clear the EXTI pending bit
        // If not cleared, the ISR will re-fire immediately upon return â€” infinite loop
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_6);
    }

    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Section 2: HW-201 IR Sensor on PA7 (EXTI line 7)
    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_7)) {
        SensorMessage_t msg;
        msg.type = SENSOR_IR;

        // HW-201 is active LOW: LOW = object detected, HIGH = clear
        // Read pin and invert: value=1 means obstacle present, value=0 means clear
        msg.value = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) ? 1 : 0;

        xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);
    }

    // If either queue send woke a higher-priority task, trigger an immediate context switch.
    // portYIELD_FROM_ISR sets the PendSV interrupt pending.
    // PendSV fires after all ISRs complete, performing the context switch to SensorTask.
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### What Happens on a Complete Measurement Cycle

```
T=0ms     SensorTask fires: PC7 TRIG goes HIGH
T=0.010ms TRIG goes LOW (10Âµs pulse complete)
T=0.010ms HC-SR04 internally fires 8x 40kHz pulses

T=0.600ms ECHO pin (PB6) goes HIGH
          â†’ EXTI9_5_IRQHandler fires (rising edge)
          â†’ TIM5->CNT = 0; TIM5 starts counting

          [Sound travels to obstacle and back]

T=2.340ms ECHO pin (PB6) goes LOW  (1740Âµs echo = ~30cm)
          â†’ EXTI9_5_IRQHandler fires (falling edge)
          â†’ TIM5 stopped at CNT = 1740
          â†’ xQueueSendFromISR posts {ULTRASONIC, 1740}
          â†’ SensorTask unblocks, computes 1740/58 = 30cm
          â†’ Posts LED_LEVEL_1 to xLEDQueue
          â†’ LEDControllerTask fades in LED4 only
```

---

## 6. HW-201 IR Proximity Sensor

### How It Works

The HW-201 is an infrared proximity sensor consisting of:
- An **IR LED** that continuously emits 38kHz modulated infrared light
- An **IR photodetector** that receives reflected infrared
- A **comparator circuit** with adjustable threshold (via potentiometer)

When an object enters the sensing zone and reflects IR back to the photodetector, the comparator output goes **LOW** (active LOW logic). When the object is removed, the output returns **HIGH**.

### Electrical Characteristics

| Property | Value |
|---|---|
| Supply voltage | 3.3V â€“ 5V |
| Output type | Open-collector, active LOW |
| Detection range | 2cm â€“ 30cm (adjustable via potentiometer) |
| Output idle state | HIGH (no obstacle) |
| Output active state | LOW (obstacle detected) |
| Response time | <1ms |

### Our Connection

| Signal | MCU Pin | Configuration |
|---|---|---|
| OUT | PA7 | Input, pull-up enabled, both-edge EXTI |

The internal pull-up on PA7 ensures a clean HIGH signal when no obstacle is present (open-collector output floats HIGH through pull-up).

### Adjusting Sensitivity

The blue potentiometer on the HW-201 module adjusts the comparator threshold â€” essentially setting the detection range. Clockwise = more sensitive (longer range). For our use case, set it to detect objects at 5â€“10cm (the same range where all LEDs should be fully lit by the ultrasonic sensor).

> ðŸ“¸ **[Hardware Photo: HW-201 IR sensor mounted on breadboard next to HC-SR04, potentiometer visible]** *(Contribute one via PR!)*

### IR Priority Over Ultrasonic

The IR sensor has **higher behavioral priority** than the ultrasonic sensor. When the IR detects an obstacle:
- All four LEDs immediately blink at 100% duty cycle
- Ultrasonic distance commands to `xLEDQueue` are ignored by `LEDControllerTask` (the `ir_active` flag suppresses them)
- This continues until IR reports the obstacle is removed

**Design rationale**: The IR sensor is detecting a close-range, binary event (something is right there or it isn't). This is more urgent and more certain than a slow-updating distance estimate. Giving IR absolute priority provides unambiguous visual feedback.

---

## 7. IR ISR Walkthrough

The IR sensor shares `EXTI9_5_IRQHandler` with the HC-SR04. Here is the IR-specific path:

```c
// Inside EXTI9_5_IRQHandler â€” PA7 section:

if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_7)) {

    SensorMessage_t msg;
    msg.type = SENSOR_IR;

    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) {
        // â”€â”€ FALLING EDGE: PA7 went LOW â”€â”€
        // Active LOW output asserted â†’ obstacle is now present
        msg.value = 1;  // 1 = obstacle detected

        // SensorTask will receive this and send LED_CMD_IR_ON to xLEDQueue
        // LEDControllerTask will enter IR override mode: all LEDs blink at 100%
    } else {
        // â”€â”€ RISING EDGE: PA7 went HIGH â”€â”€
        // Active LOW output released â†’ obstacle has been removed
        msg.value = 0;  // 0 = obstacle cleared

        // SensorTask will receive this and send LED_CMD_IR_OFF to xLEDQueue
        // LEDControllerTask will exit IR override mode, return to ultrasonic control
    }

    xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7); // clear pending bit â€” mandatory
}
```

### IR Event Flow

```
Obstacle enters range:
  PA7 goes LOW â†’ EXTI fires â†’ {SENSOR_IR, value=1} â†’ xSensorQueue
    â†’ SensorTask wakes â†’ sends LED_CMD_IR_ON â†’ xLEDQueue
      â†’ LEDControllerTask wakes â†’ ir_active=1 â†’ all LEDs blink 100%

Obstacle removed:
  PA7 goes HIGH â†’ EXTI fires â†’ {SENSOR_IR, value=0} â†’ xSensorQueue
    â†’ SensorTask wakes â†’ sends LED_CMD_IR_OFF â†’ xLEDQueue
      â†’ LEDControllerTask wakes â†’ ir_active=0 â†’ resumes ultrasonic mode
```

**Total latency from obstacle detection to first LED blink**: typically <5ms (one FreeRTOS scheduler tick + queue processing time). In practice, the blink appears instantaneous to the human eye.

---

## 8. Timing Diagram

The complete HC-SR04 measurement sequence, to scale:

```
MCU Output  PC7 (TRIG)
            â”€â”€â”€â”€â”€â”    â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
                 â”‚    â”‚                    (idle LOW)
            _____â”‚10Âµsâ”‚___________________________________________________

HC-SR04     Internal burst (not visible on MCU pins)
            Fires 8x 40kHz pulses immediately after TRIG falling edge
            â–‘â–‘â–‘â–‘â–‘â–‘â–‘â–‘â–‘  (burst â‰ˆ 200Âµs)

MCU Input   PB6 (ECHO)
            â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                              â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
                                  â”‚                              â”‚
                                  â”‚â—„â”€â”€â”€â”€â”€â”€ echo duration â”€â”€â”€â”€â”€â”€â”€â–ºâ”‚
                                  â”‚      (T_echo Âµs)             â”‚
            ______________________â”‚                              â”‚__________

EXTI fires: â–²                    â–² (rising)           â–² (falling)
            â”‚                    â”‚                    â”‚
            â”‚                    â””â”€â”€TIM5->CNT=0       â””â”€â”€TIM5 stopped
            â”‚                       TIM5 starts          msg.value=TIM5->CNT
            â”‚                                            xQueueSendFromISR

TIM5->CNT:  0â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ 0 â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• T_echo â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
                                  (counting at 1MHz = 1Âµs/tick)

SensorTask: Blocked on xSensorQueue â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–ºâ”‚ Wakes here
                                                                 â”‚ distance = T_echo/58
                                                                 â”‚ post to xLEDQueue

Timeline (example: obstacle at 30cm, T_echo â‰ˆ 1740Âµs):
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  0Âµs     : MCU asserts TRIG HIGH
  10Âµs    : MCU releases TRIG LOW
  ~210Âµs  : HC-SR04 finishes internal burst, asserts ECHO HIGH â†’ EXTI rising
  ~1950Âµs : Reflected echo received, ECHO goes LOW â†’ EXTI falling
             TIM5->CNT â‰ˆ 1740 â†’ distance = 1740/58 â‰ˆ 30cm
  ~1960Âµs : SensorTask unblocked, LEDControllerTask receives LED_LEVEL_1
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Total MCU response time from echo to LED update: ~10Âµs (one ISR + two queue ops)
```

### IR Sensor Timing

```
PA7 (IR OUT)  [Active LOW â€” pull-up holds HIGH when idle]

Idle:       â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
                                                                            
Object in:  â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                               â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
                           â”‚                               â”‚
                           â”‚â—„â”€â”€â”€â”€â”€â”€ obstacle present â”€â”€â”€â”€â”€â–ºâ”‚
                           â”‚                               â”‚
                           â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                           â–² FALLING EDGE                  â–² RISING EDGE
                           â”‚ EXTI fires                    â”‚ EXTI fires
                           â”‚ msg={IR, value=1}             â”‚ msg={IR, value=0}
                           â”‚ â†’ LED_CMD_IR_ON               â”‚ â†’ LED_CMD_IR_OFF
                           â”‚ â†’ all LEDs blink 100%         â”‚ â†’ return to ultrasonic
```

---

## Summary

| Component | Pin | Timer Used | ISR Handler | FreeRTOS Queue |
|---|---|---|---|---|
| HC-SR04 TRIG | PC7 | â€” (GPIO output) | â€” | â€” |
| HC-SR04 ECHO | PB6 | TIM5 (1Âµs stopwatch) | EXTI9_5_IRQHandler | â†’ xSensorQueue |
| HW-201 IR OUT | PA7 | â€” | EXTI9_5_IRQHandler | â†’ xSensorQueue |

The design cleanly separates concerns: ISRs capture raw hardware events with minimum overhead (a timer read + a queue post), while SensorTask performs all calculations and dispatches LED commands. No floating-point math, no string formatting, no blocking operations occur inside any ISR.

---

> **Navigation**: [â† 05 OTA Update System](05_ota_update.md) | [Home](../README.md) | [07 Button & GPIO â†’](07_button_gpio.md)
