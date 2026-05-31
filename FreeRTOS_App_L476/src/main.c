#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

// --- APPLICATION HEADER ---
typedef struct {
    uint32_t magic_number;
    uint32_t crc32;       
    uint32_t length;      
    uint32_t version;
} AppHeader_t;

__attribute__((section(".app_header"))) volatile const AppHeader_t app_header = {
    .magic_number = 0xAA55AA55,
    .crc32 = 0xFFFFFFFF,
    .length = 0xFFFFFFFF,
    .version = 1
};

// --- ENUMS & STRUCTS ---
typedef enum {
    CMD_PATTERN_CASCADE,
    CMD_PATTERN_FLASH,
    CMD_UPDATE_DISTANCE,
    CMD_OBSTACLE_START,
    CMD_OBSTACLE_STOP,
    CMD_CLEAR_LEDS
} LEDCommandType;

typedef struct {
    LEDCommandType cmd;
    uint32_t value; // Distance in cm
} LEDCmd_t;

typedef enum {
    MSG_DISTANCE,
    MSG_OBSTACLE_START,
    MSG_OBSTACLE_STOP
} SensorMsgType;

typedef struct {
    SensorMsgType type;
    uint32_t value; // Pulse width in microseconds
} SensorMsg_t;

// --- HANDLES ---
QueueHandle_t xLEDQueue;
QueueHandle_t xRXQueue;
QueueHandle_t xSensorQueue;
UART_HandleTypeDef huart1;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
volatile uint8_t sensor_running = 0;

// --- PROTOTYPES ---
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM5_Init(void);
static void MX_PWM_Init(void);
void SetLEDs(uint32_t l1, uint32_t l2, uint32_t l3, uint32_t l4);

void HeartbeatTask(void *argument);
void ButtonMonitorTask(void *argument);
void LEDControllerTask(void *argument);
void TerminalTask(void *argument);
void SensorTask(void *argument);

// --- PRINTF OVERRIDE ---
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

// --- MAIN ---
int main(void)
{
  SCB->VTOR = 0x08008000;
  HAL_Init();
  SystemClock_Config();
  
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM5_Init();
  MX_PWM_Init(); // Safe HAL-based PWM initialization!

  printf("\r\n\r\n========================================\r\n");
  printf("  Fancy FreeRTOS Application Started!\r\n");
  printf("  CPU Clock: %lu MHz\r\n", HAL_RCC_GetHCLKFreq() / 1000000);
  printf("========================================\r\n");

  xLEDQueue = xQueueCreate(10, sizeof(LEDCmd_t));
  xRXQueue = xQueueCreate(64, sizeof(char)); 
  xSensorQueue = xQueueCreate(10, sizeof(SensorMsg_t));

  xTaskCreate(HeartbeatTask, "Heartbeat", 128, NULL, 1, NULL);
  xTaskCreate(ButtonMonitorTask, "Buttons", 256, NULL, 3, NULL);
  xTaskCreate(LEDControllerTask, "LEDControl", 512, NULL, 2, NULL);
  xTaskCreate(TerminalTask, "Terminal", 512, NULL, 2, NULL); 
  xTaskCreate(SensorTask, "Sensors", 256, NULL, 4, NULL);

  vTaskStartScheduler();
  while (1) {}
}

// --- TASKS ---
void TerminalTask(void *argument)
{
    char rxBuffer[64];
    uint8_t rxIndex = 0;
    char c;

    printf("\r\nType 'help' for commands.\r\n> ");
    fflush(stdout);

    while (1)
    {
        if (xQueueReceive(xRXQueue, &c, portMAX_DELAY) == pdPASS)
        {
            printf("%c", c);
            fflush(stdout);

            if (c == '\r' || c == '\n') {
                rxBuffer[rxIndex] = '\0'; 
                
                if (rxIndex > 0) {
                    printf("\r\n");
                    
                    if (strcmp(rxBuffer, "help") == 0) {
                        printf("Available Commands:\r\n");
                        printf("  led cascade  - Run Knight Rider pattern\r\n");
                        printf("  led flash    - Flashes all LEDs\r\n");
                        printf("  sensor start - Start distance stream\r\n");
                        printf("  sensor stop  - Stop distance stream\r\n");
                        printf("  status       - Print system status\r\n");
                    }
                    else if (strcmp(rxBuffer, "led cascade") == 0) {
                        printf("[CLI] Executing CASCADE pattern...\r\n");
                        LEDCmd_t msg = {CMD_PATTERN_CASCADE, 0};
                        xQueueSend(xLEDQueue, &msg, 0);
                    }
                    else if (strcmp(rxBuffer, "led flash") == 0) {
                        printf("[CLI] Executing FLASH pattern...\r\n");
                        LEDCmd_t msg = {CMD_PATTERN_FLASH, 0};
                        xQueueSend(xLEDQueue, &msg, 0);
                    }
                    else if (strcmp(rxBuffer, "sensor start") == 0) {
                        printf("[CLI] Starting Real-Time Sensors...\r\n");
                        sensor_running = 1;
                    }
                    else if (strcmp(rxBuffer, "sensor stop") == 0) {
                        printf("[CLI] Stopping Real-Time Sensors.\r\n");
                        sensor_running = 0;
                        LEDCmd_t msg = {CMD_CLEAR_LEDS, 0};
                        xQueueSend(xLEDQueue, &msg, 0);
                    }
                    else if (strcmp(rxBuffer, "status") == 0) {
                        printf("[STATUS] System is running perfectly. Uptime: %lu ms\r\n", HAL_GetTick());
                    }
                    else {
                        printf("Unknown command: %s\r\n", rxBuffer);
                    }
                }
                
                rxIndex = 0; 
                printf("> ");
                fflush(stdout);
            }
            else if (c == '\b' || c == 127) { 
                if (rxIndex > 0) rxIndex--;
            }
            else {
                if (rxIndex < sizeof(rxBuffer) - 1) {
                    rxBuffer[rxIndex++] = c;
                }
            }
        }
    }
}

void SensorTask(void *argument)
{
    SensorMsg_t msg;
    uint32_t last_trigger = 0;
    
    while(1) {
        if (sensor_running && (HAL_GetTick() - last_trigger > 500)) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET); 
            uint32_t start = TIM5->CNT; 
            while((TIM5->CNT - start) < 10); 
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET); 
            
            last_trigger = HAL_GetTick();
        }

        if (xQueueReceive(xSensorQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS) {
            // ONLY process sensors if the CLI 'sensor start' command is active!
            if (sensor_running) {
                if (msg.type == MSG_DISTANCE) {
                    uint32_t pulse = msg.value;
                    uint32_t dist = pulse / 58;
                    
                    if (dist > 0 && dist < 400) {
                        printf("\r\n[HC-SR04] Distance: %lu cm\r\n> ", dist);
                        LEDCmd_t ledCmd = {CMD_UPDATE_DISTANCE, dist};
                        xQueueSend(xLEDQueue, &ledCmd, 0);
                    }
                    fflush(stdout);
                }
                else if (msg.type == MSG_OBSTACLE_START) {
                    printf("\r\n[HW-201] WARNING! OBSTACLE DETECTED!\r\n> ");
                    fflush(stdout);
                    LEDCmd_t ledCmd = {CMD_OBSTACLE_START, 0};
                    xQueueSend(xLEDQueue, &ledCmd, 0);
                }
                else if (msg.type == MSG_OBSTACLE_STOP) {
                    printf("\r\n[HW-201] Obstacle Removed.\r\n> ");
                    fflush(stdout);
                    LEDCmd_t ledCmd = {CMD_OBSTACLE_STOP, 0};
                    xQueueSend(xLEDQueue, &ledCmd, 0);
                }
            }
        }
    }
}

void HeartbeatTask(void *argument)
{
  while (1)
  {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ButtonMonitorTask(void *argument)
{
  uint8_t blueBtn_last = 0;
  uint8_t extBtn_last = 1;

  while (1)
  {
    uint8_t blueBtn_current = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);
    uint8_t extBtn_current = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10);

    if (blueBtn_current != blueBtn_last) {
        if (blueBtn_current == GPIO_PIN_RESET) {
             printf("\r\n[INPUT] Blue Button Triggered! Sending CASCADE command...\r\n> ");
             fflush(stdout);
             LEDCmd_t msg = {CMD_PATTERN_CASCADE, 0};
             xQueueSend(xLEDQueue, &msg, 0);
        }
    }

    if (extBtn_current == GPIO_PIN_RESET && extBtn_last == GPIO_PIN_SET) {
        printf("\r\n[INPUT] External Button Triggered! Sending FLASH command...\r\n> ");
        fflush(stdout);
        LEDCmd_t msg = {CMD_PATTERN_FLASH, 0};
        xQueueSend(xLEDQueue, &msg, 0);
    }

    blueBtn_last = blueBtn_current;
    extBtn_last = extBtn_current;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void SetLEDs(uint32_t l1, uint32_t l2, uint32_t l3, uint32_t l4) {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, l1); // LED 1 (PB4)
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, l2); // LED 2 (PB5)
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, l3); // LED 3 (PB3)
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, l4); // LED 4 (PA8)
}

void LEDControllerTask(void *argument)
{
  LEDCmd_t cmd;
  uint8_t obstacle_active = 0;
  uint8_t blink_state = 0;
  uint32_t saved_l1=0, saved_l2=0, saved_l3=0, saved_l4=0;

  while (1)
  {
    // If IR sensor is active, timeout every 100ms to blink!
    TickType_t wait_time = obstacle_active ? pdMS_TO_TICKS(100) : portMAX_DELAY;

    if (xQueueReceive(xLEDQueue, &cmd, wait_time) == pdPASS)
    {
       if (cmd.cmd == CMD_CLEAR_LEDS) {
           saved_l1 = saved_l2 = saved_l3 = saved_l4 = 0;
           if (!obstacle_active) SetLEDs(0, 0, 0, 0);
       }
       else if (cmd.cmd == CMD_UPDATE_DISTANCE) {
           uint32_t dist = cmd.value;
           uint32_t l1=0, l2=0, l3=0, l4=0;
           
           if (dist > 30) {
               // All off
           } else if (dist <= 5) {
               l1 = 1000; l2 = 1000; l3 = 1000; l4 = 1000;
           } else {
               // Smoothly distribute 4000 total PWM steps across the 25cm range (30cm -> 5cm)
               float progress = (30.0f - (float)dist) / 25.0f; 
               float total_pwm = progress * 4000.0f;
               
               l4 = (total_pwm > 1000) ? 1000 : (uint32_t)total_pwm;
               total_pwm -= 1000; if(total_pwm < 0) total_pwm = 0;
               
               l3 = (total_pwm > 1000) ? 1000 : (uint32_t)total_pwm;
               total_pwm -= 1000; if(total_pwm < 0) total_pwm = 0;
               
               l2 = (total_pwm > 1000) ? 1000 : (uint32_t)total_pwm;
               total_pwm -= 1000; if(total_pwm < 0) total_pwm = 0;
               
               l1 = (total_pwm > 1000) ? 1000 : (uint32_t)total_pwm;
           }
           
           // If obstacle is present, just update memory, don't change lights!
           saved_l1 = l1; saved_l2 = l2; saved_l3 = l3; saved_l4 = l4;
           if (!obstacle_active) SetLEDs(l1, l2, l3, l4);
       }
       else if (cmd.cmd == CMD_OBSTACLE_START) {
           obstacle_active = 1;
       }
       else if (cmd.cmd == CMD_OBSTACLE_STOP) {
           obstacle_active = 0;
           SetLEDs(saved_l1, saved_l2, saved_l3, saved_l4); // Instantly restore ultrasonic bar graph!
       }
       else if (cmd.cmd == CMD_PATTERN_CASCADE) {
           obstacle_active = 0; // Cancel obstacle to show pattern
           SetLEDs(0,0,0,0);
           for (int loop = 0; loop < 3; loop++) {
               SetLEDs(1000, 0, 0, 0); vTaskDelay(pdMS_TO_TICKS(75));
               SetLEDs(0, 1000, 0, 0); vTaskDelay(pdMS_TO_TICKS(75));
               SetLEDs(0, 0, 1000, 0); vTaskDelay(pdMS_TO_TICKS(75));
               SetLEDs(0, 0, 0, 1000); vTaskDelay(pdMS_TO_TICKS(75));
               SetLEDs(0, 0, 1000, 0); vTaskDelay(pdMS_TO_TICKS(75));
               SetLEDs(0, 1000, 0, 0); vTaskDelay(pdMS_TO_TICKS(75));
           }
           SetLEDs(saved_l1, saved_l2, saved_l3, saved_l4);
       }
       else if (cmd.cmd == CMD_PATTERN_FLASH) {
           obstacle_active = 0;
           for (int loop = 0; loop < 5; loop++) {
               SetLEDs(1000, 1000, 1000, 1000); vTaskDelay(pdMS_TO_TICKS(100));
               SetLEDs(0, 0, 0, 0); vTaskDelay(pdMS_TO_TICKS(100));
           }
           SetLEDs(saved_l1, saved_l2, saved_l3, saved_l4);
       }
    }
    else {
        // TIMEOUT OCCURRED! This only happens if obstacle_active is true.
        if (obstacle_active) {
            blink_state = !blink_state;
            if (blink_state) SetLEDs(1000, 1000, 1000, 1000);
            else SetLEDs(0, 0, 0, 0);
        }
    }
  }
}

// --- HARDWARE INIT ---

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

static void MX_TIM5_Init(void)
{
  __HAL_RCC_TIM5_CLK_ENABLE();
  TIM5->PSC = 80 - 1; 
  TIM5->ARR = 0xFFFFFFFF; 
  TIM5->EGR = TIM_EGR_UG; 
  TIM5->CR1 |= TIM_CR1_CEN; 
}

static void MX_PWM_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  // TIM1
  __HAL_RCC_TIM1_CLK_ENABLE();
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 79;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 1000;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  HAL_TIM_PWM_Init(&htim1);
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

  // TIM2
  __HAL_RCC_TIM2_CLK_ENABLE();
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 79;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 1000;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  HAL_TIM_PWM_Init(&htim2);
  HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

  // TIM3
  __HAL_RCC_TIM3_CLK_ENABLE();
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 79;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  HAL_TIM_PWM_Init(&htim3);
  HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);
  HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
}

static void MX_USART1_UART_Init(void)
{
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&huart1);

  HAL_NVIC_SetPriority(USART1_IRQn, 5, 0); 
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  USART1->CR1 |= USART_CR1_RXNEIE;
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Heartbeat LED
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // === LED PWM PINS (ALTERNATE FUNCTION) ===
  GPIO_InitStruct.Pin = GPIO_PIN_8; // LED4 (TIM1_CH1)
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_3; // LED3 (TIM2_CH2)
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5; // LED1 & LED2 (TIM3_CH1 & CH2)
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Buttons
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Sensors
  GPIO_InitStruct.Pin = GPIO_PIN_7; // TRIG
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_6; // ECHO
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_7; // IR OUT
  // Configure for BOTH edges so we know when object appears AND disappears!
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP; 
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

extern void xPortSysTickHandler(void);
void SysTick_Handler(void)
{
  HAL_IncTick();
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    xPortSysTickHandler();
  }
}

void USART1_IRQHandler(void)
{
  if (USART1->ISR & USART_ISR_RXNE)
  {
      char c = (char)USART1->RDR;
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xQueueSendFromISR(xRXQueue, &c, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

volatile uint32_t echo_start_time = 0;

void EXTI9_5_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // 1. HC-SR04 ECHO Pin (PB6)
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_6) != RESET)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_6);
        
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) {
            echo_start_time = TIM5->CNT; 
        } else {
            uint32_t echo_end_time = TIM5->CNT;
            uint32_t pulse_width = echo_end_time - echo_start_time;
            
            SensorMsg_t msg = {MSG_DISTANCE, pulse_width};
            xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
        }
    }

    // 2. HW-201 IR Sensor (PA7)
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_7) != RESET)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);
        
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) {
            // Falling edge: Object Detected!
            SensorMsg_t msg = {MSG_OBSTACLE_START, 0};
            xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
        } else {
            // Rising edge: Object Removed!
            SensorMsg_t msg = {MSG_OBSTACLE_STOP, 0};
            xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}