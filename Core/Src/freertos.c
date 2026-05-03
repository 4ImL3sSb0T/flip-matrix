/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>

#include "service/matrix/matrix.h"
#include "service/flip/flip_core.h"
#include "bsp/uart/uart_async.h"
#include "service/cli/port/shell_port.h"
#include "service/cli/log/log.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  uart_async_init();
  uart_async_start();
  // shell_port_init();
  // shell_port_start();

  // matrix_config_t matrix_config = {
  //   .cols = 16,
  //   .rows = 16,
  //   .topology = MATRIX_TOPO_SNAKE
  // };
  //
  // matrix_init(&matrix_config);



  // FlipFluid* flip_handle = {0};
  // flip_handle = flip_create(1.0f, 1.0f, 16, 0.6f);
  uint32_t h = 0, s = 200, v = 40;
  static uint8_t test_buffer[256];
  /* Infinite loop */
  for(;;)
  {
    h += 20;
    // matrix_fill(matrix_hsv2rgb(h % 360, s, v));
    // matrix_write_async();
    HAL_GPIO_TogglePin(BLUE_GPIO_Port, BLUE_Pin);
    memset(test_buffer, 0, 256);
    size_t len = uart_async_read(test_buffer, sizeof(test_buffer)/sizeof(test_buffer[0]), 5);
    uart_async_write(test_buffer, len, 5);
    osDelay(200);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  for (;;) { __asm volatile("nop"); }
}

/* FreeRTOS Trace 空桩函数定义 */
#if ( configUSE_TRACE_FACILITY == 1 )

__attribute__((weak)) void traceISR_ENTER(void)
{
  /* 空实现 - 无 trace 工具时留空 */
}

__attribute__((weak)) void traceISR_EXIT(void)
{
  /* 空实现 - 无 trace 工具时留空 */
}

__attribute__((weak)) void traceISR_EXIT_TO_SCHEDULER(void)
{
  /* 空实现 - 无 trace 工具时留空 */
}

#endif /* configUSE_TRACE_FACILITY */
/* USER CODE END Application */

