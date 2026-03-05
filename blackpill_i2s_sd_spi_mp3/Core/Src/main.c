/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t frequency;
    uint32_t bytes_per_sec;
    uint16_t bytes_per_block;
    uint16_t bits_per_sample;
} fmt_typedef;


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define I2S_DMA_BUFFER_SAMPLES 256
#define I2S_DMA_BUFFER_SIZE 2 * 2 * I2S_DMA_BUFFER_SAMPLES // 2 full buffers L+R samples
#define SAMPLE_FREQ 96000
#define OUTPUT_MID 32768

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_tx;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;

/* USER CODE BEGIN PV */

const char total_uptime_filename[] = "uptime.dat";

float amplifier = 0.5;

int8_t usart_rx_buffer[16];

int16_t i2s_dma_buffer[I2S_DMA_BUFFER_SIZE];
int16_t *do_buffer;

uint32_t buffers_done;
uint32_t total_uptime;

uint8_t open_next_file = 1;

FRESULT res;
DIR dir;
FIL music_file;
FILINFO music_file_info;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2S2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int _write(int fd, char* ptr, int len) {
  HAL_StatusTypeDef hstatus;

  if (fd == 1 || fd == 2) {
    hstatus = HAL_UART_Transmit(&huart1, (uint8_t *) ptr, len, HAL_MAX_DELAY);
    if (hstatus == HAL_OK)
      return len;
    else
      return -1;
  }
  return -1;
}
//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
//    if (GPIO_Pin == BTN_Pin) {
//
//        printf("BTN ");
//        GPIO_PinState pin_state = HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin);
//
//        if (pin_state == GPIO_PIN_RESET) {
//            printf("released\n");
//            open_next_file = 1;
//            //new_btn_state = 1;
//        } else {
//            printf("pressed\n");
//            //new_btn_state = 0;
//        }
//
//    }
//}

void set_i2s_freq(uint32_t freq) {

    printf("Setting I2S sample frequency to: %lu\n", freq);

    // Stop the DMA transfers
    HAL_I2S_DMAStop(&hi2s2);

    // Deinit
    HAL_I2S_DeInit(&hi2s2);

    if (freq > 0) {

        hi2s2.Init.AudioFreq = freq;

        HAL_I2S_Init(&hi2s2);

        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*) &i2s_dma_buffer, I2S_DMA_BUFFER_SIZE);

    }

}

void process_buffer(int16_t *target_buffer) {

    int16_t buf[2 * I2S_DMA_BUFFER_SAMPLES] = { 0 };
    unsigned int bytes_read = 0;

    if (f_read(&music_file, &buf, sizeof(buf), &bytes_read) == FR_OK) {

        for (int i = 0; i < 2 * I2S_DMA_BUFFER_SAMPLES; ++i) {
            buf[i] = buf[i] * amplifier;
        }

        memcpy(target_buffer, buf, sizeof(buf));

        if (bytes_read < sizeof(buf)) {

            printf("File done!\n");

            set_i2s_freq(0);

            open_next_file = 1;
        }

    }

}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    do_buffer = &i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES]; // Second half
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    do_buffer = &i2s_dma_buffer[0]; // First half
}

void process_character(char ch) {

    switch (ch) {
    case 'e':
        amplifier += 0.01;
        if (amplifier > 1)
            amplifier = 1;
        printf("Amp = %0.2f\n", amplifier);
        break;
    case 'd':
        amplifier -= 0.01;
        if (amplifier < 0)
            amplifier = 0;
        printf("Amp = %0.2f\n", amplifier);
        break;
    case 'f':
        open_next_file = 1;
        break;
    }

}

/**
 * @brief  UART Event Callback.  Fired on idle or if dma buffer half full or full.
 * @param  huart  Pointer to a UART_HandleTypeDef structure that contains
 *                the configuration information for the specified UART module.
 * @param  offset A offset counter pointing to last valid character in DMA buffer.
 * @retval None
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t offset) {

    static uint16_t last_offset = 0;

    // Ignore if called twice (which will happen on every half buffer)
    if (offset != last_offset) {

        // If wrap around reset last_size
        if (offset < last_offset)
            last_offset = 0;

        while (last_offset < offset) {
            process_character((char) usart_rx_buffer[last_offset]);
            ++last_offset;
        }

    }

}

//uint8_t BSP_SD_IsDetected(void)
//{
//    __IO uint8_t status = SD_PRESENT;
//
//    if (HAL_GPIO_ReadPin(SD_DETECT_GPIO_PORT, SD_DETECT_PIN) != GPIO_PIN_RESET) {
//        status = SD_NOT_PRESENT;
//    }
//
//    return status;
//}

FRESULT parse_wav_header(FIL *f, fmt_typedef *format) {

    char buf[512];  // 512 bytes for header - enough?
    unsigned int n;

    if (f_read(f, &buf, sizeof(buf), &n) != FR_OK && n < sizeof(buf)) {
        printf("Read error\n");
        return FR_INT_ERR;
    }

    printf("Read %d characters\n", n);

    // Ensure null terminated
    buf[sizeof(buf) - 1] = 0;

    if (strstr(buf, "RIFF") != &buf[0]) {
        printf("File is not a RIFF file\n");
        return FR_INT_ERR;
    }

    //char *ch = strstr(buf, "WAVEfmt ");

    if (strstr(buf, "WAVEfmt ") != &buf[8]) {
        printf("Can't find WAVE fmt\n");
        return FR_INT_ERR;
    }

    memcpy(format, &buf[20], sizeof(fmt_typedef));

    //char *data_pos = strstr(buf, "data"); // Consistantly fails
    int data_offset = 0;
    for (size_t i = 0; i <= sizeof(buf) - 4; ++i) {
        if (memcmp(buf + i, "data", 4) == 0) {
            data_offset = i;
            break; // Found it, so exit the loop
        }
    }

    if (data_offset == 0) {
        printf("Can not find data\n");
        return FR_INT_ERR;
    }

    printf("Data offset: %u\n", data_offset);

    if (f_lseek(f, data_offset) != FR_OK) {
        printf("Can not seek\n");
        return FR_INT_ERR;
    }

    data_offset = f_tell(f);

    printf("File pos = %d\n", data_offset);

    return FR_OK;
}

/* USER CODE BEGIN 0 */

void list_wav_files(void) {
    DIR dir_list;
    FILINFO fno;
    FRESULT res;
    uint8_t count = 0;

    printf("\n=== WAV Files on SD Card ===\n");

    res = f_findfirst(&dir_list, &fno, "/", "*.wav");

    while (res == FR_OK && fno.fname[0] != '\0') {
        count++;
        printf("%d. %s (%lu bytes)\n", count, fno.fname, fno.fsize);
        res = f_findnext(&dir_list, &fno);
    }

    f_closedir(&dir_list);

    if (count == 0) {
        printf("⚠️  No WAV files found!\n");
        printf("Please copy .wav files to SD card root directory\n");
    } else {
        printf("Total: %d WAV files found\n", count);
    }

    printf("============================\n\n");
}

/* USER CODE END 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();
  MX_FATFS_Init();
  MX_I2S2_Init();
  /* USER CODE BEGIN 2 */


  printf("\r\n~ SD card demo by __kiwih__ ~\r\n\r\n");

  HAL_Delay(1000); //a short delay is important to let the SD card settle

  //some variables for FatFs
  FATFS FatFs;     //Fatfs handle
  //FIL fil;         //File handle
  FRESULT fres;    //Result after operations
  FIL music_file;
  FIL SDFile;
  UINT rbytes, wbytes;
  uint32_t total_uptime = 0;

  // 1️⃣ Mount filesystem (SPI still slow)
  fres = f_mount(&FatFs, "", 1);

  if (fres != FR_OK) {
      printf("f_mount error (%i)\r\n", fres);
      while(1);
  }

  printf("SD Mounted OK\n");

  // 2️⃣ NOW increase SPI speed
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) {
      printf("SPI speed increase failed\r\n");
      // ❌ ERROR: Can't return -1 if this is in main() or void function
      // FIX: Use while(1) or Error_Handler() instead
      while(1);  // Changed from return -1
  }

  printf("SPI speed increased\n");

  // 3️⃣ Now safe to use filesystem at high speed
  DWORD free_clusters, free_sectors, total_sectors;
  FATFS* getFreeFs;

  fres = f_getfree("", &free_clusters, &getFreeFs);

  if (fres != FR_OK) {
      printf("f_getfree error (%i)\r\n", fres);
      while(1);
  }

  //Formula comes from ChaN's documentation
  total_sectors = (getFreeFs->n_fatent - 2) * getFreeFs->csize;
  free_sectors = free_clusters * getFreeFs->csize;

  printf("SD card stats:\r\n%10lu KiB total drive space.\r\n%10lu KiB available.\r\n", total_sectors / 2, free_sectors / 2);

  printf("\n\n---------------------\n");
  printf("Starting music player (SPI Mode)\n");

  // After this line:
  if (f_opendir(&dir, "/") != FR_OK) {
      printf("Failed to open root directory\r\n");
      while(1);
  }
  printf("Directory opened\n");

  printf("Directory opened\n");

  list_wav_files();

  // ✅ ADD THIS: Load first WAV file automatically
  printf("Loading first WAV file...\n");
  res = f_findfirst(&dir, &music_file_info, "/", "*.wav");

  if (res == FR_OK && music_file_info.fname[0] != '\0') {
      printf("Found: %s\n", music_file_info.fname);
      open_next_file = 1;  // Trigger file opening in main loop
  } else {
      printf("No WAV files found - waiting...\n");
  }

  HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*) &i2s_dma_buffer, I2S_DMA_BUFFER_SIZE);

  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, (uint8_t*) &usart_rx_buffer, sizeof(usart_rx_buffer));

  // ❌ ERROR: Closing file that was never opened
  // FIX: Only close if you opened it first
  // f_close(&fil);  // Commented out - fil was never opened

  // ❌ ERROR: Unmounting while system is still running
  // FIX: Only unmount when shutting down, not during normal operation
  // f_mount(NULL, "", 0);  // Commented out - premature unmount

           //Now let's try and write a file "write.txt"





  /* Make sure CS is HIGH before init */
//  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
//  HAL_Delay(10);

  /* Variables */
  //FRESULT fres;













  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
   uint32_t now = 0, next_blink = 500, next_tick = 1000, loop_cnt = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  now = uwTick;



	          if (now >= next_tick) {

	              //printf("Tick %lu (loop=%lu bd=%lu)\n", now / 1000, loop_cnt, buffers_done);

	              if (((now / 1000) % 60) == 0) {

	                  ++total_uptime;

	                  printf("Updating total uptime to %lu minutes\n", total_uptime);

	                  // Update the total uptime file
	                  if (f_open(&SDFile, total_uptime_filename, FA_OPEN_EXISTING | FA_WRITE) == FR_OK) {
	                      if (f_write(&SDFile, &total_uptime, sizeof(total_uptime), (void*) &wbytes) != FR_OK) {
	                          printf("Unable to write\n");
	                      }
	                      f_close(&SDFile);
	                  } else {
	                      printf("Unable to open file\n");
	                  }

	              }

	              open_next_file = 0;

	              loop_cnt = 0;
	              next_tick = now + 1000;

	          }



	        	  if (open_next_file) {

	        	      f_close(&music_file);

	        	      // Try to find next WAV file
	        	      res = f_findnext(&dir, &music_file_info);

	        	      // If no more files or error, restart from beginning
	        	      if (res != FR_OK || music_file_info.fname[0] == '\0') {
	        	          printf("Restarting directory search...\n");

	        	          // ✅ Close and reopen directory
	        	          f_closedir(&dir);

	        	          if (f_opendir(&dir, "/") != FR_OK) {
	        	              printf("Failed to reopen directory\n");
	        	              open_next_file = 0;
	        	              continue;  // Skip this iteration
	        	          }

	        	          // Find first WAV file
	        	          res = f_findfirst(&dir, &music_file_info, "/", "*.wav");

	        	          if (res != FR_OK || music_file_info.fname[0] == '\0') {
	        	              printf("No WAV files found on SD card!\n");
	        	              open_next_file = 0;
	        	              continue;  // Skip this iteration
	        	          }
	        	      }

	        	      printf("Next file: %s\n", music_file_info.fname);

	        	      // ✅ Try to open the file
	        	      if (f_open(&music_file, music_file_info.fname, FA_READ) != FR_OK) {
	        	          printf("Unable to open %s\n", music_file_info.fname);
	        	          open_next_file = 1;  // Try next file
	        	          continue;  // Skip this iteration
	        	      }

	        	      fmt_typedef wav_format;

	        	      if (parse_wav_header(&music_file, &wav_format) != FR_OK) {
	        	          printf("Unable to parse header for %s\n", music_file_info.fname);
	        	          f_close(&music_file);
	        	          open_next_file = 1;  // Try next file
	        	      } else {

	        	          printf("Wav format: %d\n", wav_format.format);
	        	          printf("Wav channels: %d\n", wav_format.channels);
	        	          printf("Wav frequency: %lu\n", wav_format.frequency);
	        	          printf("Wav bytes per sec: %lu\n", wav_format.bytes_per_sec);
	        	          printf("Wav bytes per block: %d\n", wav_format.bytes_per_block);
	        	          printf("Wav bits per sample: %d\n", wav_format.bits_per_sample);

	        	          set_i2s_freq(wav_format.frequency);

	        	          open_next_file = 0;
	        	      }

	        	  }

	          if (do_buffer) {

	              process_buffer(do_buffer);

	              ++buffers_done;
	              do_buffer = 0;
	          }

	          ++loop_cnt;
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 12;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2S2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S2_Init(void)
{

  /* USER CODE BEGIN I2S2_Init 0 */

  /* USER CODE END I2S2_Init 0 */

  /* USER CODE BEGIN I2S2_Init 1 */

  /* USER CODE END I2S2_Init 1 */
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_96K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S2_Init 2 */

  /* USER CODE END I2S2_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : SPI1_CS_Pin */
  GPIO_InitStruct.Pin = SPI1_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI1_CS_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
