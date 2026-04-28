# Matrix LED Service Design

## Overview

WS2812B LED matrix service on STM32H750VBTx. Provides a double-buffered framebuffer abstraction over the WS2812B SPI driver, exposing basic drawing primitives and batch write for high-throughput frame updates (target: FLIP fluid simulation output).

## Architecture

```
FLIP/User thread            FreeRTOS timer ISR
      │                           │
      │ matrix_write_pixels()     │ matrix_flush()
      │ matrix_set_pixel()        │   swap(back, front)
      │ matrix_fill()             │   ws2812b_encode(front, temp)
      │ matrix_clear()            │   SPI_DMA_start(temp)
      ▼                           ▼
  ┌──────────────┐       ┌──────────────┐
  │  back_buffer │  swap │ front_buffer │
  │  (always RW) │◄─────►│ (owned by SPI│
  └──────────────┘       │  DMA)        │
                         └──────────────┘
                                │
                                ▼
                         SPI → WS2812B strip
```

Double-buffer strategy: user thread writes back buffer freely, timer callback atomically swaps pointers then starts DMA on front buffer. No mutex needed — only the swap is a critical section (portENTER_CRITICAL).

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

exit_code_t matrix_start(uint32_t refresh_rate_hz);
exit_code_t matrix_stop(void);
exit_code_t matrix_write_async(void);           // immediate flush (no timer wait)

exit_code_t matrix_write_pixels(const uint32_t *data, uint32_t len);
exit_code_t matrix_set_pixel(uint32_t row, uint32_t col, uint32_t rgb);
exit_code_t matrix_fill(uint32_t rgb);
exit_code_t matrix_clear(void);

uint32_t matrix_pixel_count(void);
uint32_t matrix_rows(void);
uint32_t matrix_cols(void);
```

`matrix_write_pixels` expects data in physical LED order. `matrix_set_pixel` takes logical (row, col) and applies topology mapping internally.

## Memory

Compile-time configurable max LED count. Static allocation only — no heap.

```c
#define MATRIX_MAX_LEDS 256  // default 16×16

static uint32_t fb_a[MATRIX_MAX_LEDS];       //  1 KB
static uint32_t fb_b[MATRIX_MAX_LEDS];       //  1 KB
static uint8_t  spi_temp[MATRIX_MAX_LEDS * 64]; // 16 KB (for 256 LEDs)
```

`spi_temp` sizing: each LED needs 48B SPI-encoded data; reset frame needs 64B per LED. Use `max(48, 64) * N = 64 * N`.

## Thread Safety

- Only one context writes back buffer (user thread)
- Only one context reads front buffer (SPI DMA)
- Swap is atomic (pointer reassignment within `portENTER_CRITICAL`)
- DMA completion signaled via binary semaphore for deinit/stop cleanup

## Startup / Shutdown

```
matrix_init:
  validate rows×cols ≤ MATRIX_MAX_LEDS
  validate WS2812B interface linked
  zero both framebuffers
  ws2812b_init()
  back = fb_a, front = fb_b

matrix_start(hz):
  create FreeRTOS software timer, period = 1000/hz ms

matrix_stop:
  delete timer
  wait DMA done (semaphore)

matrix_deinit:
  stop if running
  wait DMA done
  ws2812b_deinit()
```

## Topology Mapping

```c
static uint32_t xy_to_index(uint32_t row, uint32_t col) {
  if (topology == MATRIX_TOPO_SNAKE && (row & 1)) {
    col = cols - 1 - col;
  }
  return row * cols + col;
}
```

Used by `matrix_set_pixel` only. `matrix_write_pixels` passes through directly.

## Dependencies

- `driver_ws2812b.h` / `driver_ws2812b_interface.h` — low-level WS2812B SPI driver
- `service/tools/common_def.h` — `exit_code_t`
- FreeRTOS software timer + binary semaphore
- `<string.h>` for memset/memcpy
