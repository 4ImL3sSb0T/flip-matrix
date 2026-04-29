# Matrix LED Service Design

## Overview

WS2812B LED matrix service on STM32H750VBTx. The current design uses manual refresh: drawing APIs update a back framebuffer, and `matrix_write_async()` explicitly submits the frame through SPI DMA.

The module intentionally does not own a periodic refresh task or timer.

## Architecture

```
FLIP/User task
      │
      │ matrix_write_buffer()
      │ matrix_set_pixel()
      │ matrix_fill()
      │ matrix_clear()
      ▼
  ┌──────────────┐
  │  back_buffer │
  │  (pending)   │
  └──────┬───────┘
         │ matrix_write_async()
         ▼
  ┌──────────────┐
  │ front_buffer │
  │ (snapshot)   │
  └──────┬───────┘
         │ ws2812b_write_async(front_buffer, spi_temp)
         ▼
  HAL_SPI_Transmit_DMA(spi_temp) -> WS2812B strip
```

`matrix_write_async()` waits for the previous DMA transfer up to `MATRIX_DMA_WAIT_TIMEOUT_MS`, swaps front/back, calls `driver_ws2812b` to encode and start DMA, and returns without waiting for the new transfer to finish.

## API

```c
typedef enum {
  MATRIX_TOPO_PROGRESSIVE = 0,
  MATRIX_TOPO_SNAKE      = 1
} matrix_topo_t;

typedef struct {
  uint32_t rows;
  uint32_t cols;
  matrix_topo_t topology;
} matrix_config_t;

exit_code_t matrix_init(const matrix_config_t *config);
exit_code_t matrix_deinit(void);

exit_code_t matrix_write_async(void);

exit_code_t matrix_write_buffer(const uint32_t *data, uint32_t len);
exit_code_t matrix_set_pixel(uint32_t row, uint32_t col, uint32_t rgb);
exit_code_t matrix_fill(uint32_t rgb);
exit_code_t matrix_clear(void);

uint32_t matrix_pixel_count(void);
uint32_t matrix_rows(void);
uint32_t matrix_cols(void);
```

`matrix_write_buffer()` expects data in logical row-major order. `matrix_write_buffer()` and `matrix_set_pixel()` apply topology mapping internally.

## Memory

Compile-time configurable max LED count. Static allocation only.

```c
#define MATRIX_MAX_LEDS 256

static uint32_t fb_a[MATRIX_MAX_LEDS];
static uint32_t fb_b[MATRIX_MAX_LEDS];
static uint8_t  spi_temp[MATRIX_MAX_LEDS * (64 + 48)];
```

Each LED uses 48 bytes of color encoding. The current reset segment reserves 64 bytes per LED from `WS2812B_EACH_RESET_BIT_FRAME_LEN / 8`.

## Thread Safety

- `matrix_mutex` protects back/front pointer swaps and framebuffer writes.
- `spi_temp` is rewritten by `driver_ws2812b` only from `matrix_write_async()` while holding `matrix_mutex`.
- DMA busy state is exposed by `driver_ws2812b` and implemented by the interface layer.
- Matrix APIs are task-context APIs and should not be called from ISR.

## Startup / Shutdown

```
matrix_init:
  validate rows * cols <= MATRIX_MAX_LEDS
  validate tx length fits uint16_t
  zero framebuffers and spi_temp
  create matrix_mutex
  ws2812b_init()

matrix_deinit:
  wait current DMA up to MATRIX_DMA_WAIT_TIMEOUT_MS
  abort on timeout
  ws2812b_deinit()
  delete matrix_mutex
```

## Topology Mapping

```c
static uint32_t xy_to_index(uint32_t row, uint32_t col)
{
  if (matrix_cfg.topology == MATRIX_TOPO_SNAKE && (row & 1)) {
    col = matrix_cfg.cols - 1 - col;
  }
  return row * matrix_cfg.cols + col;
}
```

## Dependencies

- `driver_ws2812b.h` / `driver_ws2812b.c`
- `driver_ws2812b_interface.h` / `driver_ws2812b_interface.c`
- `service/tools/common_def.h`
- FreeRTOS mutex and semaphore
- `<string.h>`
