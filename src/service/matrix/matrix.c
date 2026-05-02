#include "matrix.h"
#include "bsp/ws2812b/driver_ws2812b.h"
#include "bsp/ws2812b/driver_ws2812b_interface.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

#ifndef MATRIX_MAX_LEDS
  #define MATRIX_MAX_LEDS 256
#endif

#ifndef MATRIX_DMA_WAIT_TIMEOUT_MS
  #define MATRIX_DMA_WAIT_TIMEOUT_MS 20
#endif

#define MATRIX_RESET_BYTES_PER_LED (WS2812B_EACH_RESET_BIT_FRAME_LEN / 8)
#define MATRIX_COLOR_BYTES_PER_LED 48
#define MATRIX_TX_BYTES_PER_LED    (MATRIX_RESET_BYTES_PER_LED + MATRIX_COLOR_BYTES_PER_LED)

static uint32_t fb_a[MATRIX_MAX_LEDS];
static uint32_t fb_b[MATRIX_MAX_LEDS];
static uint32_t *front_buffer;
static uint32_t *back_buffer;

static uint8_t spi_temp[MATRIX_MAX_LEDS * MATRIX_TX_BYTES_PER_LED];
static uint8_t dma_buffer[MATRIX_MAX_LEDS * MATRIX_TX_BYTES_PER_LED] __attribute__((section(".dma_buffer"), aligned(32), used));

static matrix_config_t matrix_cfg;
static bool initialized;

static ws2812b_handle_t ws2812b_handle;
static SemaphoreHandle_t matrix_mutex;

static uint32_t xy_to_index(uint32_t row, uint32_t col)
{
  if (matrix_cfg.topology == MATRIX_TOPO_SNAKE && (row & 1)) {
    col = matrix_cfg.cols - 1 - col;
  }
  return row * matrix_cfg.cols + col;
}

static uint32_t matrix_led_count(void)
{
  return matrix_cfg.rows * matrix_cfg.cols;
}

static uint32_t matrix_tx_len(uint32_t led_count)
{
  return led_count * MATRIX_TX_BYTES_PER_LED;
}

static exit_code_t matrix_commit_locked(void)
{
  uint32_t led_count = matrix_led_count();
  uint32_t tx_len = matrix_tx_len(led_count);
  uint32_t *tmp;
  uint8_t ret;

  if (ws2812b_wait_async_done(&ws2812b_handle, MATRIX_DMA_WAIT_TIMEOUT_MS) != 0) {
    (void)ws2812b_abort_async(&ws2812b_handle);
    return EXIT_TIMEOUT;
  }

  tmp = front_buffer;
  front_buffer = back_buffer;
  back_buffer = tmp;

  ret = ws2812b_write_async(&ws2812b_handle, front_buffer, led_count,
                            dma_buffer, tx_len, MATRIX_DMA_WAIT_TIMEOUT_MS);
  if (ret == 6) {
    tmp = front_buffer;
    front_buffer = back_buffer;
    back_buffer = tmp;
    return EXIT_NO_RESOURCE;
  }
  if (ret != 0) {
    tmp = front_buffer;
    front_buffer = back_buffer;
    back_buffer = tmp;
    return EXIT_BUSY;
  }

  return EXIT_OK;
}

exit_code_t matrix_init(const matrix_config_t *config)
{
  if (!config) return EXIT_INVALID_PARAM;
  if (config->rows == 0 || config->cols == 0) return EXIT_INVALID_PARAM;

  uint32_t led_count = config->rows * config->cols;
  if (led_count > MATRIX_MAX_LEDS) return EXIT_NO_RESOURCE;
  if (matrix_tx_len(led_count) > UINT16_MAX) return EXIT_NO_RESOURCE;

  matrix_cfg = *config;

  memset(fb_a, 0, sizeof(fb_a));
  memset(fb_b, 0, sizeof(fb_b));
  memset(spi_temp, 0, sizeof(spi_temp));

  front_buffer = fb_a;
  back_buffer = fb_b;

  matrix_mutex = xSemaphoreCreateMutex();
  if (!matrix_mutex) return EXIT_NO_MEMORY;

  DRIVER_WS2812B_LINK_INIT(&ws2812b_handle, ws2812b_handle_t);
  DRIVER_WS2812B_LINK_SPI_10MHZ_INIT(&ws2812b_handle, ws2812b_interface_spi_10mhz_init);
  DRIVER_WS2812B_LINK_SPI_DEINIT(&ws2812b_handle, ws2812b_interface_spi_deinit);
  DRIVER_WS2812B_LINK_SPI_WRITE_COMMAND(&ws2812b_handle, ws2812b_interface_spi_write_cmd);
  DRIVER_WS2812B_LINK_SPI_START_DMA(&ws2812b_handle, ws2812b_interface_spi_start_dma);
  DRIVER_WS2812B_LINK_SPI_WAIT_DMA_DONE(&ws2812b_handle, ws2812b_interface_spi_wait_dma_done);
  DRIVER_WS2812B_LINK_SPI_ABORT_DMA(&ws2812b_handle, ws2812b_interface_spi_abort_dma);
  DRIVER_WS2812B_LINK_DELAY_MS(&ws2812b_handle, ws2812b_interface_delay_ms);
  DRIVER_WS2812B_LINK_DEBUG_PRINT(&ws2812b_handle, ws2812b_interface_debug_print);

  if (ws2812b_init(&ws2812b_handle) != 0) {
    vSemaphoreDelete(matrix_mutex);
    matrix_mutex = NULL;
    return EXIT_HW_FAILURE;
  }

  initialized = true;
  return EXIT_OK;
}

exit_code_t matrix_deinit(void)
{
  if (!initialized) return EXIT_NOT_INITIALIZED;

  if (ws2812b_wait_async_done(&ws2812b_handle, MATRIX_DMA_WAIT_TIMEOUT_MS) != 0) {
    (void)ws2812b_abort_async(&ws2812b_handle);
  }
  ws2812b_deinit(&ws2812b_handle);

  if (matrix_mutex) {
    vSemaphoreDelete(matrix_mutex);
    matrix_mutex = NULL;
  }

  initialized = false;
  return EXIT_OK;
}

exit_code_t matrix_write_async(void)
{
  exit_code_t ret;

  if (!initialized) return EXIT_NOT_INITIALIZED;

  xSemaphoreTake(matrix_mutex, portMAX_DELAY);
  ret = matrix_commit_locked();
  xSemaphoreGive(matrix_mutex);

  return ret;
}

exit_code_t matrix_write_buffer(const uint32_t *data, uint32_t len)
{
  if (!initialized) return EXIT_NOT_INITIALIZED;
  if (!data) return EXIT_INVALID_PARAM;

  uint32_t led_count = matrix_led_count();
  if (len != led_count) return EXIT_INVALID_PARAM;

  xSemaphoreTake(matrix_mutex, portMAX_DELAY);
  for (uint32_t row = 0; row < matrix_cfg.rows; row++) {
    for (uint32_t col = 0; col < matrix_cfg.cols; col++) {
      uint32_t logical_idx = row * matrix_cfg.cols + col;
      uint32_t physical_idx = xy_to_index(row, col);
      back_buffer[physical_idx] = data[logical_idx];
    }
  }
  xSemaphoreGive(matrix_mutex);

  return EXIT_OK;
}

exit_code_t matrix_set_pixel(uint32_t row, uint32_t col, uint32_t rgb)
{
  if (!initialized) return EXIT_NOT_INITIALIZED;
  if (row >= matrix_cfg.rows || col >= matrix_cfg.cols) return EXIT_INVALID_PARAM;

  xSemaphoreTake(matrix_mutex, portMAX_DELAY);
  back_buffer[xy_to_index(row, col)] = rgb;
  xSemaphoreGive(matrix_mutex);

  return EXIT_OK;
}

exit_code_t matrix_fill(uint32_t rgb)
{
  if (!initialized) return EXIT_NOT_INITIALIZED;

  xSemaphoreTake(matrix_mutex, portMAX_DELAY);
  for (uint32_t i = 0; i < matrix_led_count(); i++) {
    back_buffer[i] = rgb;
  }
  xSemaphoreGive(matrix_mutex);

  return EXIT_OK;
}

exit_code_t matrix_clear(void)
{
  return matrix_fill(0x00000000);
}

uint32_t matrix_pixel_count(void)
{
  return matrix_led_count();
}

uint32_t matrix_rows(void)
{
  return matrix_cfg.rows;
}

uint32_t matrix_cols(void)
{
  return matrix_cfg.cols;
}

uint32_t matrix_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t matrix_hsv2rgb(uint16_t h, uint8_t s, uint8_t v)
{
  if (s == 0) {
    return ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
  }

  h %= 360;
  uint8_t region = h / 60;
  uint8_t rem = (uint8_t)(((uint32_t)(h % 60) * 255) / 60);

  uint8_t p = (uint8_t)(((uint32_t)v * (255 - s)) >> 8);
  uint8_t q = (uint8_t)(((uint32_t)v * (255 - (uint16_t)(((uint32_t)s * rem) >> 8))) >> 8);
  uint8_t t = (uint8_t)(((uint32_t)v * (255 - (uint16_t)(((uint32_t)s * (255 - rem)) >> 8))) >> 8);

  uint8_t r, g, b;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }

  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
