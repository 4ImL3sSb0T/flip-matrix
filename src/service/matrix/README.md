# Matrix LED Service

基于 WS2812B 的 LED 矩阵服务，负责 RGB 双缓冲、拓扑映射、SPI 编码和异步 DMA 发送。

## 架构

```
用户任务
  │
  ├── matrix_write_buffer()
  ├── matrix_set_pixel()
  ├── matrix_fill()
  └── matrix_clear()
        │
        ▼
  back_buffer (仅更新待显示帧)
        │
        │ matrix_write_async()
        ▼
  swap(back, front)
        │
        ▼
  ws2812b_write_async(front_buffer, spi_temp)
        │
        ▼
  HAL_SPI_Transmit_DMA(spi_temp)
        │
        ▼
  SPI DMA 完成中断释放 busy 状态
```

写入接口只修改 `back_buffer`，不会自动刷新显示。需要显示新内容时，调用 `matrix_write_async()` 手动提交一帧。提交时会通过 WS2812B 驱动等待上一帧 DMA 完成，最长等待 `MATRIX_DMA_WAIT_TIMEOUT_MS`，默认 20ms；超时会 abort 当前 DMA 并返回 `EXIT_TIMEOUT`。

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
```

### 生命周期

| 函数 | 说明 |
|------|------|
| `matrix_init(config)` | 初始化双缓冲、互斥锁、WS2812B 驱动和 SPI DMA 接口 |
| `matrix_deinit()` | 等待或终止当前 DMA，反初始化 SPI，释放互斥锁 |

### 写入

| 函数 | 说明 |
|------|------|
| `matrix_write_buffer(data, len)` | 批量写入逻辑坐标帧数据，内部自动拓扑映射，不刷新 |
| `matrix_set_pixel(row, col, rgb)` | 写入单个逻辑像素，内部自动拓扑映射，不刷新 |
| `matrix_fill(rgb)` | 填充全部 LED，不刷新 |
| `matrix_clear()` | 清空全部 LED，不刷新 |

`matrix_write_buffer()` 的 `data` 按逻辑行列顺序排列：`data[row * cols + col]`。蛇形走线时，模块会自动转换为物理 LED 序号。

### 显示控制

| 函数 | 说明 |
|------|------|
| `matrix_write_async()` | 手动提交 back buffer：等待上一帧 DMA 完成，交换 front/back，调用 WS2812B 驱动编码并启动新的 SPI DMA |

`matrix_write_async()` 启动 DMA 后立即返回，不等待本帧发送完成。下一次提交会先等待上一帧完成，等待超时由 `MATRIX_DMA_WAIT_TIMEOUT_MS` 控制。

### 查询

`matrix_pixel_count()` / `matrix_rows()` / `matrix_cols()`

## 使用示例

```c
matrix_config_t cfg = {
  .rows = 16,
  .cols = 16,
  .topology = MATRIX_TOPO_SNAKE,
};

matrix_init(&cfg);

compute_frame(frame_data);
matrix_write_buffer(frame_data, matrix_pixel_count());
matrix_write_async();

matrix_set_pixel(0, 0, 0xFF0000);
matrix_set_pixel(0, 1, 0x00FF00);
matrix_write_async();

matrix_deinit();
```

## 拓扑映射

```c
static uint32_t xy_to_index(uint32_t row, uint32_t col)
{
  if (matrix_cfg.topology == MATRIX_TOPO_SNAKE && (row & 1)) {
    col = matrix_cfg.cols - 1 - col;
  }
  return row * matrix_cfg.cols + col;
}
```

`matrix_write_buffer()` 和 `matrix_set_pixel()` 使用逻辑坐标并经过拓扑映射。`matrix_fill()` / `matrix_clear()` 是全量写入，不需要坐标映射。

## 内存

编译常量 `MATRIX_MAX_LEDS` 默认 256，可通过 CMake 编译定义覆盖。静态分配，无堆：

| 缓冲区 | 大小 |
|--------|------|
| `fb_a` | `MATRIX_MAX_LEDS * 4` 字节 |
| `fb_b` | `MATRIX_MAX_LEDS * 4` 字节 |
| `spi_temp` | `MATRIX_MAX_LEDS * (64 + 48)` 字节，位于 `.dma_buffer` |

默认 256 LED 时，两个 framebuffer 共 2KB，`spi_temp` 约 28KB。`spi_temp` 包含 reset 低电平段和颜色编码段，并通过 `.dma_buffer` 放在 D2 SRAM `0x30000000`。该区域由 MPU 配成 non-cacheable，避免 SPI DMA 读取到 DCache 中尚未写回的旧数据。

## 线程安全

| 资源 | 保护方式 |
|------|----------|
| `back_buffer` / `front_buffer` 指针 | `matrix_mutex` |
| `spi_temp` | 由 `driver_ws2812b` 在 `matrix_write_async()` 持有 `matrix_mutex` 时重编码，放在 non-cacheable `.dma_buffer` |
| SPI DMA busy 状态 | `driver_ws2812b` 通过底层 interface 等待/abort，DMA 完成中断在 interface 中释放状态 |

不要从 ISR 调用 `matrix_*` API。这些 API 使用 FreeRTOS mutex 和可能的有限等待。

## SPI 传输

- 颜色顺序：GRB
- 编码由 `driver_ws2812b` 完成：16 SPI bit 编码 1 个 WS2812B bit，`0xFFF8` 表示逻辑 1，`0xE000` 表示逻辑 0
- 每 LED 颜色数据 48 字节
- 每 LED 前置 reset 数据 64 字节，来自 `WS2812B_EACH_RESET_BIT_FRAME_LEN / 8`
- DMA 启动后本帧不阻塞等待，完成中断清除 busy 状态

## 依赖

- `src/bsp/ws2812b/driver_ws2812b.c` — WS2812B 编码和异步写接口
- `src/bsp/ws2812b/driver_ws2812b_interface.c` — SPI DMA 硬件适配
- `Core/Src/spi.c`
- `Core/Src/dma.c`
- FreeRTOS mutex 和 semaphore
