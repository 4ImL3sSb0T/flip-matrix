#include "driver_ws2812b_interface.h"
#include "spi.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdarg.h>
#include <stdio.h>

static SemaphoreHandle_t dma_tx_sem;
static volatile uint8_t dma_tx_busy;

uint8_t ws2812b_interface_spi_10mhz_init(void)
{
  dma_tx_sem = xSemaphoreCreateBinary();
  dma_tx_busy = 0;
  return (dma_tx_sem != NULL) ? 0 : 1;
}

uint8_t ws2812b_interface_spi_deinit(void)
{
  if (dma_tx_sem) {
    vSemaphoreDelete(dma_tx_sem);
    dma_tx_sem = NULL;
  }
  dma_tx_busy = 0;
  return (HAL_SPI_DeInit(&hspi1) == HAL_OK) ? 0 : 1;
}

uint8_t ws2812b_interface_spi_write_cmd(uint8_t *buf, uint16_t len)
{
  if (ws2812b_interface_spi_start_dma(buf, len) != 0) {
    return 1;
  }
  if (ws2812b_interface_spi_wait_dma_done(500) != 0) {
    HAL_SPI_DMAStop(&hspi1);
    dma_tx_busy = 0;
    return 1;
  }
  return 0;
}

uint8_t ws2812b_interface_spi_start_dma(uint8_t *buf, uint16_t len)
{
  if (!dma_tx_sem || !buf || len == 0 || dma_tx_busy) {
    return 1;
  }

  (void)xSemaphoreTake(dma_tx_sem, 0);
  dma_tx_busy = 1;

  if (HAL_SPI_Transmit_DMA(&hspi1, buf, len) != HAL_OK) {
    dma_tx_busy = 0;
    return 1;
  }

  return 0;
}

uint8_t ws2812b_interface_spi_wait_dma_done(uint32_t timeout_ms)
{
  if (!dma_tx_busy) {
    return 0;
  }

  if (xSemaphoreTake(dma_tx_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return 1;
  }

  return 0;
}

uint8_t ws2812b_interface_spi_abort_dma(void)
{
  if (!dma_tx_busy) {
    return 0;
  }

  if (HAL_SPI_DMAStop(&hspi1) != HAL_OK) {
    return 1;
  }

  dma_tx_busy = 0;
  (void)xSemaphoreTake(dma_tx_sem, 0);

  return 0;
}

void ws2812b_interface_delay_ms(uint32_t ms)
{
  vTaskDelay(pdMS_TO_TICKS(ms));
}

void ws2812b_interface_debug_print(const char *const fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1 && dma_tx_sem) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    dma_tx_busy = 0;
    xSemaphoreGiveFromISR(dma_tx_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}
