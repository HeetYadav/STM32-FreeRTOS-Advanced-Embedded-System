# Hardware PWM & LED Control

> **Document Version**: 1.0 | **Target MCU**: STM32L476RG | **Timers Used**: TIM1, TIM2, TIM3 (PWM) + TIM5 (Ultrasonic timing)

---

## Table of Contents

1. [What is PWM?](#1-what-is-pwm)
2. [Why Hardware PWM over Software PWM](#2-why-hardware-pwm-over-software-pwm)
3. [STM32 Timer Architecture](#3-stm32-timer-architecture)
4. [Timer Configuration Table](#4-timer-configuration-table)
5. [Why 1 kHz PWM Frequency?](#5-why-1-khz-pwm-frequency)
6. [🐛 The TIM2 Conflict Bug](#6--the-tim2-conflict-bug)
7. [MX_PWM_Init Walkthrough](#7-mx_pwm_init-walkthrough)
8. [SetLEDs() Function Explained](#8-setleds-function-explained)
9. [Distance Fading Math](#9-distance-fading-math)
10. [IR Priority Override](#10-ir-priority-override)

---

## 1. What is PWM?

**Pulse Width Modulation (PWM)** is a technique for controlling the *average power* delivered to a load by rapidly switching a digital output ON and OFF. The CPU doesn't output an analog voltage: it outputs a square wave. The ratio of time spent HIGH to the total period is called the **duty cycle**, and it determines how bright an LED appears, how fast a motor spins, or how loud a buzzer sounds.

```
Duty Cycle = (Time HIGH / Period) × 100%
```

Here's how different duty cycles look as waveforms:

```
0%: Always OFF
_________________________________________________________
                                                        

25%: ON for 1/4 of each period
‾‾‾‾‾_______________‾‾‾‾‾_______________‾‾‾‾‾___________
 ON  |     OFF      | ON  |     OFF      | ON  |

50%: ON for half of each period (half brightness)
‾‾‾‾‾‾‾‾‾‾__________‾‾‾‾‾‾‾‾‾‾__________‾‾‾‾‾‾‾‾‾‾______
   ON      |  OFF   |   ON      |  OFF   |   ON

75%: ON for 3/4 of each period (bright)
‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾____‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾____‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾___
      ON         |OF|      ON         |OF|      ON

100%: Always ON
‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
```

Because LEDs respond to average current, and human eyes have persistence of vision, a 50% duty cycle at 1 kHz looks like a LED at half brightness: even though it is technically blinking 1,000 times per second, far too fast to perceive.

> [!NOTE]
> PWM is not limited to LEDs. The same technique drives servo motors (by pulse width, not duty cycle), DC motors (by average voltage), audio DACs (1-bit audio output), switching power supplies, and heater elements. Mastering PWM on an STM32 opens the door to most actuator control applications.

---

## 2. Why Hardware PWM over Software PWM?

Software PWM means your CPU manually toggles a GPIO pin in a loop or timer ISR:

```c
// ❌ Software PWM: do NOT do this in a real-time system
while (1) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);    // LED ON
    delay_us(750);  // 75% duty cycle high time
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);  // LED OFF
    delay_us(250);  // 75% duty cycle low time
}
```

This approach has fatal problems in an RTOS environment:

| Problem | Consequence |
|---|---|
| **CPU-bound** | The CPU is stuck in a delay loop doing nothing useful |
| **Jitter** | FreeRTOS context switches interrupt the delay, making duty cycles inaccurate |
| **Blocks other tasks** | SensorTask, TerminalTask, ButtonMonitorTask can't run |
| **Doesn't scale** | 4 LEDs = 4 blocking loops = impossible |
| **Resolution limited** | Minimum step size depends on OS tick rate (1 ms) |

**Hardware PWM** offloads all waveform generation to a dedicated timer peripheral:

```c
// ✅ Hardware PWM: the timer does everything after this one call
__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 750);  // 75% duty cycle, set and forget
```

Once configured, the timer peripheral generates the waveform **autonomously**: no CPU, no interrupts, no jitter. The FreeRTOS scheduler and all other tasks run at full speed completely unaffected. Changing the brightness is a single register write that takes effect immediately on the next PWM cycle.

---

## 3. STM32 Timer Architecture

The STM32L476 has 11 timers. Three types matter for this project:

```
STM32L476RG Timer Hierarchy
│
├── TIM1  (Advanced Control Timer)
│     • APB2 bus (up to 80 MHz after x2 multiplier)
│     • 16-bit, complementary outputs with dead-time insertion
│     • Channels: CH1=PA8, CH1N=PA7, CH2=PA9, CH3=PA10, CH4=PA11
│     • We use: CH1 → PA8 → LED4 (farthest)
│
├── TIM2  (General Purpose, 32-bit)
│     • APB1 bus (up to 80 MHz after x2 multiplier)
│     • 32-bit counter: unique among GP timers, ideal for long timing intervals
│     • Channels: CH1=PA0, CH2=PB3, CH3=PA2, CH4=PA3
│     • We use: CH2 → PB3 → LED3 PWM
│     • ⚠️ Originally used for ultrasonic timing: caused a conflict (see §6)
│
├── TIM3  (General Purpose, 16-bit)
│     • APB1 bus (up to 80 MHz after x2 multiplier)
│     • Channels: CH1=PB4, CH2=PB5, CH3=PB0, CH4=PB1
│     • We use: CH1 → PB4 → LED1 (nearest), CH2 → PB5 → LED2
│
└── TIM5  (General Purpose, 32-bit)
      • APB1 bus
      • Repurposed as 1 MHz microsecond stopwatch for HC-SR04 echo timing
      • No PWM channels used: purely counter mode
      • No GPIO conflict with LED pins
```

**Alternate Function (AF) Pin Mapping** is the mechanism that connects a timer channel to a physical GPIO pin. On STM32, every pin has multiple possible functions; you configure which one you want via the GPIO Alternate Function registers:

```c
// When HAL_TIM_PWM_Start() is called for TIM3_CH1, HAL internally does:
// 1. Configure PB4 as AF2 (Alternate Function 2 = TIM3)
// 2. Enable the timer's PWM output on CH1
// PB4 now outputs the timer waveform: no CPU involvement needed
GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;  // AF2 maps to TIM3 on PB4
```

---

## 4. Timer Configuration Table

All four PWM channels are configured identically for simplicity: 1 kHz output frequency, 0:1000 CCR range (providing 0.1% brightness resolution).

| Timer | Channel | GPIO Pin | LED | APB Bus | Prescaler | ARR (Period) | PWM Frequency | CCR Range |
|-------|---------|----------|-----|---------|-----------|--------------|---------------|-----------|
| TIM1  | CH1     | PA8      | LED4 (farthest) | APB2 | 79 | 1000 | 1 kHz | 0:1000 |
| TIM2  | CH2     | PB3      | LED3            | APB1 | 79 | 1000 | 1 kHz | 0:1000 |
| TIM3  | CH1     | PB4      | LED1 (nearest)  | APB1 | 79 | 1000 | 1 kHz | 0:1000 |
| TIM3  | CH2     | PB5      | LED2            | APB1 | 79 | 1000 | 1 kHz | 0:1000 |

**Frequency calculation:**

```
Timer Clock = 80 MHz (both APB buses run at 80 MHz on this config)
Prescaler   = 79   → Timer ticks at 80,000,000 / (79+1) = 1,000,000 Hz = 1 MHz
ARR         = 1000 → Timer resets every 1000 ticks → Period = 1000 / 1,000,000 = 1 ms
PWM Freq    = 1 / 1ms = 1000 Hz = 1 kHz
CCR=500     → 500/1000 = 50% duty cycle → half brightness
```

> [!TIP]
> Using ARR=1000 (instead of, say, ARR=255) gives **0.1% brightness resolution**: 1001 discrete levels instead of 256. This produces visually smooth fading without any perceptible stepping, which matters for the cascade distance effect.

---

## 5. Why 1 kHz PWM Frequency?

PWM frequency selection involves two competing constraints:

**Lower bound: flicker threshold:**

The human visual system perceives flicker below approximately 50:60 Hz. At 100 Hz, most people see flicker in peripheral vision. At 500 Hz, it's invisible to all humans. Our 1 kHz is well clear of any perceptible flicker.

**Upper bound: audible noise:**

Components on PCBs (capacitors, inductors, even LED driver circuits) can resonate at audible frequencies (20 Hz: 20 kHz). A PWM signal in this range causes a physical vibration that you can hear as a whine or buzz. At 1 kHz, this is theoretically within audible range: but bare LEDs without inductive loads produce no audible noise at this frequency. For applications with motors or inductors, pushing PWM above 20 kHz (supersonic) eliminates audible noise entirely.

**Our choice: 1 kHz:**

- ✅ Well above 60 Hz flicker threshold: no visible flicker
- ✅ No inductive loads in our circuit: no audible noise concern
- ✅ Simple math: prescaler=79, ARR=1000 gives exactly 1 kHz with a 1 MHz timer clock
- ✅ CCR range 0:1000 gives convenient 0.1% resolution
- ✅ Timer counts to 1000 in 1 ms: same order of magnitude as FreeRTOS 1 ms tick

---

## 6. 🐛 The TIM2 Conflict Bug

### Symptom

During development, the original plan was to use **TIM2** for measuring HC-SR04 ultrasonic echo pulses (running at 1 MHz for microsecond-accuracy timing), and **TIM2_CH2 on PB3** for LED3 PWM. Both features worked independently during unit testing.

When integrated, however, **LED3 never lit up**: its PWM output was silent: while the ultrasonic sensor worked fine.

### Investigation

Checking the STM32L476 Reference Manual (RM0351) revealed the fundamental problem: **TIM2_CH2 IS pin PB3**. A single timer cannot simultaneously:

1. Run as a free-running 1 MHz counter (for echo pulse measurement), AND
2. Generate a PWM waveform on one of its capture/compare channels

These are mutually exclusive modes. The timer's counter drives all channels: if the counter is configured for input capture / free-running, the PWM output channel cannot generate a clean waveform.

### Root Cause

Timer resources on STM32 (and all microcontrollers) are shared between the counter and all its channels. **TIM2 was fully consumed by the ultrasonic timing function.** There was no spare capacity to simultaneously output PWM on TIM2_CH2.

### Resolution

Migrate the ultrasonic echo timing to **TIM5**: a 32-bit general purpose timer with no GPIO pins required for our use case (we only need its counter, not its output channels).

| | Before | After |
|---|---|---|
| Ultrasonic timing | TIM2 (free-running, 1 MHz) | **TIM5** (free-running, 1 MHz) |
| LED3 PWM | TIM2_CH2 / PB3: **broken** | TIM2_CH2 / PB3: **works** |
| LED1 PWM | TIM3_CH1 / PB4 | TIM3_CH1 / PB4 (unchanged) |
| LED2 PWM | TIM3_CH2 / PB5 | TIM3_CH2 / PB5 (unchanged) |
| LED4 PWM | TIM1_CH1 / PA8 | TIM1_CH1 / PA8 (unchanged) |

TIM5 is 32-bit (counts to 4,294,967,295): ideal for echo timing because HC-SR04 echoes can be up to ~38 ms long, requiring a minimum counter range of 38,000 ticks at 1 MHz. A 16-bit timer (max 65,535 at 1 MHz = 65.5 ms) would technically work but TIM5's 32-bit range gives comfortable headroom.

> [!IMPORTANT]
> Always check the full **Alternate Function mapping table** in the MCU datasheet *before* assigning timers to pins. This is a common mistake in STM32 projects: two features look independent on paper but share the same hardware resource.

---

## 7. MX_PWM_Init Walkthrough

The PWM initialization follows a strict order: initialize the timer base, then configure each channel, then start the output. Reversing the order causes incorrect configurations to be applied.

```c
// File: FreeRTOS_App_L476/Core/Src/pwm_control.c

TIM_HandleTypeDef htim1, htim2, htim3;    // Global timer handles used by SetLEDs()

/**
 * @brief  Initialize all four PWM timer channels.
 *         Called once from main() before FreeRTOS scheduler starts.
 */
void MX_PWM_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    /* ── TIM3: LED1 (CH1/PB4) and LED2 (CH2/PB5) ──────────────────────── */

    // Step 1: Initialize the timer base
    // Prescaler=79: 80MHz / (79+1) = 1MHz timer clock
    // Period=1000:  1MHz / 1001  ≈ 1kHz PWM frequency
    // CounterMode UP: counts 0→1000 then resets
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 79;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 1000;     // ARR register value
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    // AutoReloadPreload=ENABLE: new ARR values take effect at next update event,
    // not mid-cycle. Prevents glitches if we change frequency at runtime.
    HAL_TIM_PWM_Init(&htim3);

    // Step 2: Configure output compare mode
    // PWM1: output is HIGH while counter < CCR, LOW while counter >= CCR
    // This gives duty cycle = CCR / (ARR+1) * 100%
    sConfigOC.OCMode       = TIM_OCMODE_PWM1;
    sConfigOC.Pulse        = 0;               // Start at 0% duty cycle (LED off)
    sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;  // Active HIGH: LED anode to 3.3V
    sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);  // PB4 → LED1
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2);  // PB5 → LED2
    // Note: same sConfigOC used for both channels: both start at 0%

    // Step 3: Start PWM output on the GPIO pin
    // This also configures the GPIO alternate function (AF2 for TIM3)
    // MUST call after ConfigChannel: starting before configuring gives garbage waveform
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

    /* ── TIM2: LED3 (CH2/PB3) ──────────────────────────────────────────── */
    htim2.Instance               = TIM2;     // 32-bit GP timer: same config as TIM3
    htim2.Init.Prescaler         = 79;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 1000;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    sConfigOC.Pulse = 0;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2);  // PB3 → LED3
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

    /* ── TIM1: LED4 (CH1/PA8) ──────────────────────────────────────────── */
    // TIM1 is an Advanced Control Timer: requires one extra step:
    // HAL_TIMEx_PWMN_Start() or enabling the main output (MOE bit in BDTR register)
    // HAL_TIM_PWM_Start() handles this automatically for CH1 on TIM1.
    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 79;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 1000;
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    htim1.Init.RepetitionCounter = 0;     // Advanced timer only: not used
    HAL_TIM_PWM_Init(&htim1);

    sConfigOC.Pulse = 0;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);  // PA8 → LED4
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    // For TIM1, HAL also sets MOE (Main Output Enable) in BDTR: required for
    // advanced timers to actually drive the pin. General purpose timers don't need this.
}
```

**Why order matters:**

`HAL_TIM_PWM_Init()` → `HAL_TIM_PWM_ConfigChannel()` → `HAL_TIM_PWM_Start()`

- `Init` configures the timer base (prescaler, period, counter mode)
- `ConfigChannel` writes the CCR (capture/compare register) and output mode: requires the timer to already be initialized so the register addresses are valid
- `Start` enables the timer counter and configures the GPIO alternate function: if called before `ConfigChannel`, the CCR contains garbage and the first cycle output is undefined

---

## 8. SetLEDs() Function Explained

Once initialized, changing LED brightness is a single HAL macro call per channel:

```c
/**
 * @brief  Set PWM duty cycle for all four LEDs simultaneously.
 *
 * @param  l1  Duty cycle for LED1 (nearest,  PB4/TIM3_CH1), range 0:1000
 * @param  l2  Duty cycle for LED2           (PB5/TIM3_CH2), range 0:1000
 * @param  l3  Duty cycle for LED3           (PB3/TIM2_CH2), range 0:1000
 * @param  l4  Duty cycle for LED4 (farthest, PA8/TIM1_CH1), range 0:1000
 *
 * @note   This function is called from LEDControllerTask: never from an ISR.
 *         __HAL_TIM_SET_COMPARE() is a direct register write, not a blocking call.
 */
void SetLEDs(uint32_t l1, uint32_t l2, uint32_t l3, uint32_t l4)
{
    // __HAL_TIM_SET_COMPARE(handle, channel, compare_value) expands to:
    // handle->Instance->CCR<n> = compare_value;
    // It writes directly to the Capture/Compare Register: one 32-bit memory write.
    // The timer hardware reads the new CCR value at the next period boundary
    // (because AutoReloadPreload is enabled, so updates are shadow-buffered).
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, l1);  // LED1: CCR1 of TIM3
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, l2);  // LED2: CCR2 of TIM3
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, l3);  // LED3: CCR2 of TIM2
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, l4);  // LED4: CCR1 of TIM1
}
```

**Why `__HAL_TIM_SET_COMPARE()` is the right tool here:**

This macro expands to a *single peripheral register write*: it does not:
- Stop or restart the timer (no glitch, no missed pulse)
- Require the timer to be paused
- Disable interrupts
- Block for any amount of time

The new CCR value is latched into a **shadow register** and takes effect at the next timer update event (when the counter reaches ARR and resets to 0). This means all four LEDs update glitch-free on a 1 ms boundary. Compare this to software PWM where changing brightness mid-cycle causes a visible glitch.

---

## 9. Distance Fading Math

The cascade distance-to-brightness algorithm maps sensor readings to a smooth, sequential LED fill pattern. The design intent: as an object approaches from 30 cm away, LEDs light up one by one: LED4 (farthest from the sensor mount) first, cascading toward LED1 (nearest).

### Range Definition

```
Distance > 30 cm   →  All LEDs OFF      (object too far, ignore)
Distance = 5:30 cm →  Cascade fade      (smooth fill from LED4 → LED1)
Distance ≤ 5 cm    →  All LEDs at 100%  (maximum brightness, object very close)
```

### The Math

```c
/**
 * @brief  Compute PWM values for cascade distance fading.
 * @param  dist_cm  Measured distance in centimeters from HC-SR04
 */
void update_cascade_leds(float dist_cm)
{
    uint32_t l1, l2, l3, l4;

    if (dist_cm > 30.0f) {
        // Object too far: all off
        SetLEDs(0, 0, 0, 0);
        return;
    }

    if (dist_cm <= 5.0f) {
        // Object very close: all at 100%
        SetLEDs(1000, 1000, 1000, 1000);
        return;
    }

    // Map distance 30cm→5cm to progress 0.0→1.0
    // At 30cm: progress=0.0 (all off)
    // At 5cm:  progress=1.0 (all on)
    float progress = (30.0f - dist_cm) / 25.0f;

    // Total PWM budget: 4 LEDs × 1000 max each = 4000 units maximum
    // Distribute the budget sequentially: LED4 fills first, then LED3, etc.
    float total_pwm = progress * 4000.0f;

    // LED4 fills from 0→1000 as total_pwm goes 0→1000
    l4 = (uint32_t)fminf(total_pwm,           1000.0f);

    // LED3 fills from 0→1000 as total_pwm goes 1000→2000
    l3 = (uint32_t)fminf(fmaxf(total_pwm - 1000.0f, 0.0f), 1000.0f);

    // LED2 fills from 0→1000 as total_pwm goes 2000→3000
    l2 = (uint32_t)fminf(fmaxf(total_pwm - 2000.0f, 0.0f), 1000.0f);

    // LED1 fills from 0→1000 as total_pwm goes 3000→4000
    l1 = (uint32_t)fminf(fmaxf(total_pwm - 3000.0f, 0.0f), 1000.0f);

    SetLEDs(l1, l2, l3, l4);
}
```

### Worked Example: Distance = 17.5 cm

```
progress  = (30.0 - 17.5) / 25.0  = 12.5 / 25.0  = 0.5
total_pwm = 0.5 × 4000            = 2000.0

LED4: min(2000,          1000) = 1000  ← fully ON  (100%)
LED3: min(max(2000-1000, 0), 1000)
    = min(1000, 1000)             = 1000  ← fully ON  (100%)
LED2: min(max(2000-2000, 0), 1000)
    = min(0, 1000)                = 0     ← OFF        (0%)
LED1: min(max(2000-3000, 0), 1000)
    = min(0, 1000)                = 0     ← OFF        (0%)

Result: LED4=ON, LED3=ON, LED2=OFF, LED1=OFF
```

### Visual: What the LEDs Show at Various Distances

```
30cm: ░░░░  (all off)
25cm: ▒░░░  (LED4 dimly on, others off)
22cm: █░░░  (LED4 full, others off)
17cm: ██░░  (LED4 + LED3 full, others off)  ← 17.5cm example above
13cm: ███░  (LED4 + LED3 + LED2 full, LED1 off)
 8cm: ████▒ (all full except LED1 slightly dim)
 5cm: ████  (all full: maximum brightness)
```
<img width="1678" height="1079" alt="UltrasonicFinal" src="https://github.com/user-attachments/assets/d1dc0e36-4485-4dcb-a26f-73941972d04b" />

> [!TIP]
> The cascade fill pattern is visually intuitive: it simulates a "level" or "signal bar" that rises as the object approaches. LED4 (physically farthest from the sensor) fills first, creating the impression that the "signal" grows from back to front as the object gets closer.

---

## 10. IR Priority Override

When the HW-201 IR proximity sensor detects an object, the `EXTI9_5_IRQHandler` fires (PA7 goes LOW = active low). The LEDControllerTask checks a shared flag (`ir_object_detected`) and completely changes LED behavior:

```c
// File: FreeRTOS_App_L476/Core/Src/led_controller_task.c

void LEDControllerTask(void *pvParameters)
{
    LEDMessage_t msg;
    bool ir_object_detected = false;
    uint32_t saved_l1 = 0, saved_l2 = 0, saved_l3 = 0, saved_l4 = 0;
    TickType_t blink_toggle_tick = 0;
    bool blink_state = false;

    for (;;) {
        // Non-blocking queue receive: 10ms timeout so IR blink timer keeps running
        if (xQueueReceive(xLEDQueue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {

            if (msg.type == LED_MSG_DISTANCE) {
                // Compute what the cascade values WOULD be
                compute_cascade_values(msg.distance_cm,
                                       &saved_l1, &saved_l2, &saved_l3, &saved_l4);

                if (!ir_object_detected) {
                    // IR not active: apply normally
                    SetLEDs(saved_l1, saved_l2, saved_l3, saved_l4);
                }
                // If IR IS active: distance values are saved but NOT applied.
                // IR blink takes priority: saved values are waiting for when IR clears.

            } else if (msg.type == LED_MSG_IR_DETECT) {
                ir_object_detected = true;
                blink_toggle_tick = xTaskGetTickCount();
                blink_state = true;
                SetLEDs(1000, 1000, 1000, 1000);   // All on immediately

            } else if (msg.type == LED_MSG_IR_CLEAR) {
                ir_object_detected = false;
                // Restore last computed distance values immediately
                SetLEDs(saved_l1, saved_l2, saved_l3, saved_l4);
            }
        }

        // IR blink logic: runs every 10ms tick even without a new queue message
        if (ir_object_detected) {
            TickType_t now = xTaskGetTickCount();
            if ((now - blink_toggle_tick) >= pdMS_TO_TICKS(100)) {
                blink_toggle_tick = now;
                blink_state = !blink_state;
                // Toggle between 100% and 0% every 100ms = 5 Hz blink
                uint32_t val = blink_state ? 1000 : 0;
                SetLEDs(val, val, val, val);
            }
        }
    }
}
```

**The key design decision: saving distance values during IR override:**

Rather than discarding distance measurements during IR mode, the task saves the computed PWM values into `saved_l1..l4`. When the IR object is removed and `LED_MSG_IR_CLEAR` arrives, the LEDs **instantly return to the correct distance-based state** without waiting for the next sensor reading (which comes every 500 ms). This makes the transition seamless.

**Priority hierarchy (implemented in task logic, not RTOS priority):**

```
Highest → IR proximity blink (5 Hz, all LEDs, overrides everything)
Middle  → Distance cascade (smooth fade, 30cm:5cm range)
Lowest  → All off (distance > 30cm or sensor stopped)
```

> 📸 **[Hardware Photo: Four LEDs mounted on breadboard, demonstrating cascade fill at various distances]** *(Contribute one via PR!)*

---

*← [04: Secure Bootloader](04_secure_bootloader.md) | [06: Sensors & EXTI](06_sensors_exti.md) →*
