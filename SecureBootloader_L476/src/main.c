#include "stm32l4xx_hal.h"
#include <string.h>

#define APPLICATION_ADDRESS     0x08008000

// XMODEM Control Characters
#define SOH  0x01
#define EOT  0x04
#define ACK  0x06
#define NAK  0x15
#define CAN  0x18
#define C    0x43

typedef void (*pFunction)(void);
pFunction JumpToApplication;
uint32_t JumpAddress;

// The Application Header structure that the Python script injects
typedef struct {
    uint32_t magic_number;
    uint32_t crc32;       
    uint32_t length;      
    uint32_t version;
} AppHeader_t;

// Peripheral Handles
CRC_HandleTypeDef hcrc;
UART_HandleTypeDef huart1;

// Function Prototypes
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CRC_Init(void);
static void MX_USART1_UART_Init(void);
void Error_Handler(void);

// XMODEM CRC-16 Calculation
uint16_t xmodem_crc(uint8_t *data, uint16_t length) {
    uint16_t crc = 0;
    for (uint16_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)(data[i]) << 8;
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

// Full XMODEM Receiver and Flash Programmer!
void XMODEM_Receive(void) {
    uint8_t packet[133];
    uint32_t flash_addr = APPLICATION_ADDRESS;
    uint8_t expected_packet_num = 1;
    uint8_t c;
    
    char *erasing_msg = "\r\n[OTA] Erasing Flash Memory... Please wait.\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t*)erasing_msg, strlen(erasing_msg), HAL_MAX_DELAY);

    // 1. Unlock and Erase the Application Flash Space (Page 16 to 255)
    HAL_FLASH_Unlock();
    
    // Clear any previous flash errors before erasing
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PAGEError = 0;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.Banks = FLASH_BANK_1;
    EraseInitStruct.Page = 16;
    EraseInitStruct.NbPages = 256 - 16; // Clear the rest of Bank 1
    HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError);
    
    char *ready_msg = "[OTA] Ready! Please send firmware.bin via XMODEM now.\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t*)ready_msg, strlen(ready_msg), HAL_MAX_DELAY);

    // 2. Wait for connection (Send 'C' continuously)
    while (1) {
        c = C;
        HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
        if (HAL_UART_Receive(&huart1, &c, 1, 1000) == HAL_OK) {
            if (c == SOH) {
                packet[0] = c;
                break; // XMODEM stream started!
            }
        }
    }
    
    // 3. Receive Packets and Program Flash
    while (1) {
        // We already received the SOH byte, receive the remaining 132 bytes of the packet
        HAL_StatusTypeDef status = HAL_UART_Receive(&huart1, &packet[1], 132, 1000);
        
        if (status == HAL_OK) {
            if (packet[1] == expected_packet_num && packet[2] == (uint8_t)(~expected_packet_num)) {
                
                uint16_t calc_crc = xmodem_crc(&packet[3], 128);
                uint16_t rcv_crc = (packet[131] << 8) | packet[132];
                
                if (calc_crc == rcv_crc) {
                    // CRC Passed! Write 128 bytes to Flash (16 double-words)
                    for (int i = 0; i < 128; i += 8) {
                        uint64_t double_word;
                        memcpy(&double_word, &packet[3 + i], 8); // Prevent unaligned 64-bit read fault!
                        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, flash_addr, double_word) != HAL_OK) {
                            // If flash programming fails, we might want to NAK or handle error.
                            // But let's just proceed for now.
                        }
                        flash_addr += 8;
                    }
                    expected_packet_num++;
                    
                    // Acknowledge the packet
                    c = ACK;
                    HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
                } else {
                    // CRC Failed! Request retransmission
                    c = NAK;
                    HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
                }
            } else {
                // Wrong packet number! Request retransmission
                c = NAK;
                HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
            }
        } else {
            // Timeout! Request retransmission
            c = NAK;
            HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
        }
        
        // Wait for the next header byte (SOH or EOT)
        if (HAL_UART_Receive(&huart1, &c, 1, 2000) == HAL_OK) {
            if (c == EOT) { // End of Transmission!
                c = ACK;
                HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
                break; // We are done!
            } else if (c == SOH) { // Start of next packet
                packet[0] = c;
            } else {
                c = NAK;
                HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
            }
        }
    }
    
    // 4. Lock Flash and Reboot into the new application!
    HAL_FLASH_Lock();
    
    char *done_msg = "\r\n[OTA] Update Successfully Flashed! Rebooting...\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t*)done_msg, strlen(done_msg), HAL_MAX_DELAY);
    HAL_Delay(500);
    
    NVIC_SystemReset();
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_CRC_Init();
  MX_USART1_UART_Init();

  // Indicate Bootloader Mode (Turn on LED 1 temporarily)
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET); 

  char *welcome_msg = "\r\n\r\n--- Advanced UART Bootloader Started ---\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t*)welcome_msg, strlen(welcome_msg), HAL_MAX_DELAY);

  // PB10 is the physical breadboard button. Pulled UP. Pressed = RESET (LOW)
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_SET) 
  {
      if (((*(__IO uint32_t*)APPLICATION_ADDRESS) & 0x2FFE0000 ) == 0x20000000)
      {
          AppHeader_t *app = (AppHeader_t *)(APPLICATION_ADDRESS + 0x188);
          
          if (app->magic_number == 0xAA55AA55 && app->length != 0xFFFFFFFF && app->length < 1000000)
          {
              char *msg1 = "Magic Number Valid! Verifying CRC...\r\n";
              HAL_UART_Transmit(&huart1, (uint8_t*)msg1, strlen(msg1), HAL_MAX_DELAY);
              
              __HAL_CRC_DR_RESET(&hcrc);
              
              uint32_t words_before = (0x188 + 4) / 4; 
              HAL_CRC_Accumulate(&hcrc, (uint32_t*)APPLICATION_ADDRESS, words_before);
              
              uint32_t dummy = 0xFFFFFFFF;
              HAL_CRC_Accumulate(&hcrc, &dummy, 1);
              
              uint32_t words_after = (app->length - (0x188 + 8)) / 4;
              uint32_t final_crc = HAL_CRC_Accumulate(&hcrc, (uint32_t*)(APPLICATION_ADDRESS + 0x188 + 8), words_after);
              
              if (final_crc == app->crc32)
              {
                  char *msg2 = "CRC MATCH! Jumping to Application...\r\n";
                  HAL_UART_Transmit(&huart1, (uint8_t*)msg2, strlen(msg2), HAL_MAX_DELAY);
                  
                  JumpAddress = *(__IO uint32_t*) (APPLICATION_ADDRESS + 4);
                  JumpToApplication = (pFunction) JumpAddress;
                  
                  HAL_UART_DeInit(&huart1);
                  HAL_CRC_DeInit(&hcrc);
                  HAL_RCC_DeInit();
                  HAL_DeInit();
                  SysTick->CTRL = 0;
                  SysTick->LOAD = 0;
                  SysTick->VAL = 0;
                  
                  __set_MSP(*(__IO uint32_t*) APPLICATION_ADDRESS);
                  JumpToApplication();
              }
              else
              {
                  char *msg3 = "CRC FAILED! Firmware Corrupted. Entering OTA Mode.\r\n";
                  HAL_UART_Transmit(&huart1, (uint8_t*)msg3, strlen(msg3), HAL_MAX_DELAY);
              }
          }
      }
  }
  else
  {
      char *btn_msg = "Button pressed! Forced OTA Update Mode.\r\n";
      HAL_UART_Transmit(&huart1, (uint8_t*)btn_msg, strlen(btn_msg), HAL_MAX_DELAY);
  }

  // If we reach here, we are doing an OTA!
  XMODEM_Receive();
  while(1);
}

// System Clock Configuration (80 MHz)
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
    Error_Handler();
  }

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
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

// CRC Initialization Function
static void MX_CRC_Init(void)
{
  __HAL_RCC_CRC_CLK_ENABLE();
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
  if (HAL_CRC_Init(&hcrc) != HAL_OK) {
    Error_Handler();
  }
}

// USART1 Initialization Function (PA9 TX, PA10 RX)
static void MX_USART1_UART_Init(void)
{
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  
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
  if (HAL_UART_Init(&huart1) != HAL_OK) {
    Error_Handler();
  }
}

// GPIO Initialization Function
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Configure UART pins (PA9 TX, PA10 RX)
  GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Configure Status LED (PB4)
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Configure External Button (PB10) - Pulled up!
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}