# UART DMA 异步收发与 Shell 接入设计

日期：2026-05-01

## 1. 目标

本文设计一个基于 FreeRTOS、STM32 HAL UART DMA 中断机制的高效串口异步收发系统，并将其接入现有 shell。

设计目标：

- RX 使用 UART DMA circular + IDLE 事件，减少逐字节中断开销。
- TX 使用 DMA 队列化异步发送，调用方不直接等待 DMA 完成。
- shell 只依赖统一的读写接口，不直接操作 HAL UART/DMA。
- DMA 源/目标 buffer 放在 MPU non-cacheable 的 `.dma_buffer` 区域，避免 DCache 一致性问题。
- 设计为通用 UART async driver，当前先实例化 USART1，后续可以接入其它 UART。

非目标：

- 不设计复杂串口协议解析层。
- 不把 shell 和 UART HAL 直接耦合。
- 不使用逐字节 UART RX 中断作为主接收路径。

## 2. 当前工程约束

当前工程中已经具备这些基础条件：

- `USART1` 已由 CubeMX/HAL 初始化。
- `DMA1_Stream1` 用于 `USART1_RX`。
- `DMA1_Stream2` 用于 `USART1_TX`。
- `USART1_IRQn`、`DMA1_Stream1_IRQn`、`DMA1_Stream2_IRQn` 已配置中断。
- DCache 已启用。
- MPU Region 1 覆盖 `0x30000000` 起始的 32KB D2 SRAM，并配置为 non-cacheable。
- linker 已提供 `.dma_buffer` 段，并断言 `.dma_buffer <= 32KB`。
- matrix 的 SPI DMA TX buffer 已放入 `.dma_buffer`，UART DMA buffer 也必须计入同一个 32KB MPU 区域。

这意味着 UART DMA 使用的 RX DMA buffer 和 TX DMA chunk buffer 必须放在 `.dma_buffer`：

```c
__attribute__((section(".dma_buffer"), aligned(32), used))
```

普通 FreeRTOS stream buffer、shell 命令 buffer、状态结构体不需要放 `.dma_buffer`，因为 DMA 不直接访问这些对象。

## 3. 总体架构

系统分为三层：

```text
┌─────────────────────────────────────────────────────────────┐
│                         Shell Service                        │
│                                                             │
│  shellTask()                                                │
│    ├─ shell.read()  -> shell_uart_read()                     │
│    └─ shell.write() -> shell_uart_write()                    │
└───────────────────────────────┬─────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                   Shell UART Adapter                         │
│                                                             │
│  只负责把 Shell 的 read/write 函数指针适配到 uart_async API   │
│  不直接调用 HAL_UART_*                                      │
└───────────────────────────────┬─────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                    Generic uart_async Driver                 │
│                                                             │
│  RX: UART DMA circular + IDLE -> RX stream buffer            │
│  TX: TX stream buffer -> TX task -> UART DMA                 │
│  Stats: dropped/errors/events counters                       │
└───────────────────────────────┬─────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                   STM32 HAL UART + DMA                       │
│                                                             │
│  HAL_UARTEx_ReceiveToIdle_DMA()                              │
│  HAL_UART_Transmit_DMA()                                     │
│  HAL_UARTEx_RxEventCallback()                                │
│  HAL_UART_TxCpltCallback()                                   │
│  HAL_UART_ErrorCallback()                                    │
└─────────────────────────────────────────────────────────────┘
```

模块边界：

- `uart_async` 是 BSP/driver 层通用能力，不 include `shell.h`、`log.h`。
- `shell_uart_port` 是 service/adapter 层，include `shell.h`，持有 `Shell` 对象。
- `freertos.c` 只负责初始化对象和创建任务，不放复杂收发逻辑。

## 4. RX 数据流

RX 使用 circular DMA，DMA 永不因一次 IDLE 自动停止。IDLE、Half Complete、Transfer Complete 事件只作为“搬运新增数据”的触发点。

```text
外部串口输入
    │
    ▼
USART1 RDR
    │
    ▼
DMA1_Stream1
    │
    ▼
rx_dma_buf[N]  (.dma_buffer, circular)
    │
    │ IDLE / HT / TC event
    ▼
HAL_UARTEx_RxEventCallback(huart, Pos)
    │
    ▼
uart_async_on_rx_event(ctx, Pos)
    │
    ├─ 根据 last_pos 和 Pos 计算新增区间
    ├─ 从 rx_dma_buf 拷贝新增数据
    └─ xStreamBufferSendFromISR(rx_stream, ...)
          │
          ▼
rx_stream  (FreeRTOS stream buffer)
          │
          ▼
shellTask()
    shell.read() -> uart_async_read(..., 1, portMAX_DELAY)
          │
          ▼
shellHandler(shell, byte)
```

关键点：

- `HAL_UARTEx_RxEventCallback()` 的 `Pos` 表示 DMA 当前写入位置，不是本次新增字节数。
- driver 保存 `last_rx_pos`，每次事件计算 `[last_rx_pos, Pos)` 之间的新增数据。
- 如果 `Pos < last_rx_pos`，说明 circular buffer 发生回绕，需要拆成两段搬运。
- RX stream buffer 满时不阻塞 ISR，丢弃无法写入的字节并增加 `rx_dropped`。

推荐 RX 默认参数：

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| RX DMA buffer | 512 bytes | 放 `.dma_buffer`，DMA 直接写 |
| RX stream buffer | 512 bytes | 普通 FreeRTOS stream buffer |
| Trigger level | 1 byte | shell 逐字节读取更直接 |

## 5. TX 数据流

TX 使用队列化异步发送。调用方把数据写入 TX stream buffer 后返回，driver 内部 TX task 负责串行启动 DMA。

```text
shell.write(data, len)
    │
    ▼
shell_uart_write()
    │
    ▼
uart_async_write(ctx, data, len, timeout)
    │
    ├─ xStreamBufferSend(tx_stream, data, len, timeout)
    └─ 返回已接受字节数
          │
          ▼
tx_stream  (FreeRTOS stream buffer)
          │
          ▼
uart_async_tx_task()
    │
    ├─ 从 tx_stream 取一段数据
    ├─ 拷贝到 tx_dma_buf[M] (.dma_buffer)
    ├─ HAL_UART_Transmit_DMA(huart, tx_dma_buf, chunk)
    └─ 等待 tx_done semaphore
          │
          ▼
DMA1_Stream2 -> USART1 TDR
          │
          ▼
HAL_UART_TxCpltCallback()
    │
    └─ xSemaphoreGiveFromISR(tx_done)
```

关键点：

- TX DMA 永远只读 `tx_dma_buf`，这个 buffer 必须位于 `.dma_buffer`。
- `tx_dma_buf` 只有 TX task 写，DMA 完成前 TX task 不会覆盖它。
- 多任务同时调用 `uart_async_write()` 时只竞争 TX stream buffer，不会直接抢 DMA。
- TX stream buffer 满时按策略丢弃新数据并增加 `tx_dropped`。

推荐 TX 默认参数：

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| TX DMA chunk buffer | 512 bytes | 放 `.dma_buffer`，DMA 直接读 |
| TX stream buffer | 1024 bytes | 普通 FreeRTOS stream buffer |
| TX task stack | 256 words | 只做搬运和 HAL 调用 |
| TX task priority | `tskIDLE_PRIORITY + 2` | 高于 idle，低于实时控制任务 |

## 6. 状态机

### 6.1 RX 状态

```text
STOPPED
   │ uart_async_start()
   ▼
RUNNING
   │ HAL_UART_ErrorCallback()
   ▼
ERROR_RECOVERY
   │ abort + restart ReceiveToIdle DMA
   ▼
RUNNING
```

RX 错误恢复策略：

- 出现 UART overrun/framing/noise/parity error 时记录 `rx_errors`。
- 调用 `HAL_UART_AbortReceive()` 或必要的 abort API。
- 清理 `last_rx_pos = 0`。
- 重新调用 `HAL_UARTEx_ReceiveToIdle_DMA()`。
- 重新打开 circular DMA 模式下的接收。

### 6.2 TX 状态

```text
IDLE
  │ tx_stream 有数据
  ▼
DMA_BUSY
  │ HAL_UART_TxCpltCallback()
  ▼
IDLE

DMA_BUSY
  │ timeout / HAL error
  ▼
TX_RECOVERY
  │ abort transmit + drain stale semaphore
  ▼
IDLE
```

TX 错误恢复策略：

- `HAL_UART_Transmit_DMA()` 返回错误时增加 `tx_errors`，短暂 delay 或直接进入下一轮。
- 等待 TX complete 超时时调用 `HAL_UART_AbortTransmit()`。
- 清空 stale semaphore token。
- 保留 TX stream buffer 中尚未取出的数据；当前 chunk 可视为发送失败并计数。

## 7. API 设计

### 7.1 类型

```c
typedef struct {
  UART_HandleTypeDef *huart;

  uint8_t *rx_dma_buf;
  uint16_t rx_dma_size;

  uint8_t *tx_dma_buf;
  uint16_t tx_dma_size;

  size_t rx_stream_size;
  size_t tx_stream_size;

  const char *tx_task_name;
  uint16_t tx_task_stack_words;
  UBaseType_t tx_task_prio;
} uart_async_config_t;

typedef struct {
  uint32_t rx_events;
  uint32_t rx_bytes;
  uint32_t rx_dropped;
  uint32_t rx_errors;

  uint32_t tx_bytes_accepted;
  uint32_t tx_bytes_sent;
  uint32_t tx_dropped;
  uint32_t tx_done_events;
  uint32_t tx_errors;
} uart_async_stats_t;

typedef struct {
  UART_HandleTypeDef *huart;

  uint8_t *rx_dma_buf;
  uint16_t rx_dma_size;
  volatile uint16_t rx_last_pos;

  uint8_t *tx_dma_buf;
  uint16_t tx_dma_size;

  StreamBufferHandle_t rx_stream;
  StreamBufferHandle_t tx_stream;
  SemaphoreHandle_t tx_done;
  TaskHandle_t tx_task;

  volatile bool started;
  uart_async_stats_t stats;
} uart_async_t;
```

### 7.2 函数

```c
exit_code_t uart_async_init(uart_async_t *ctx,
                            const uart_async_config_t *config);

exit_code_t uart_async_start(uart_async_t *ctx);

exit_code_t uart_async_stop(uart_async_t *ctx);

size_t uart_async_read(uart_async_t *ctx,
                       uint8_t *data,
                       size_t len,
                       TickType_t timeout);

size_t uart_async_write(uart_async_t *ctx,
                        const uint8_t *data,
                        size_t len,
                        TickType_t timeout);

const uart_async_stats_t *uart_async_get_stats(uart_async_t *ctx);
void uart_async_reset_stats(uart_async_t *ctx);
```

### 7.3 HAL 回调入口

驱动需要提供三个 HAL callback 分发函数：

```c
void uart_async_on_rx_event(UART_HandleTypeDef *huart, uint16_t pos);
void uart_async_on_tx_complete(UART_HandleTypeDef *huart);
void uart_async_on_error(UART_HandleTypeDef *huart);
```

然后在全局 HAL callback 中调用：

```c
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  uart_async_on_rx_event(huart, Size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  uart_async_on_tx_complete(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  uart_async_on_error(huart);
}
```

如果未来有多个 UART 实例，`uart_async` 内部维护一个静态注册表，通过 `huart` 指针查找 `ctx`。

## 8. USART1 实例化

建议新增 USART1 port 文件：

```text
src/bsp/uart/uart_async.c
src/bsp/uart/uart_async.h
src/bsp/uart/uart1_async_port.c
src/bsp/uart/uart1_async_port.h
```

`uart1_async_port.c` 负责放置 USART1 的 DMA buffer：

```c
#define UART1_RX_DMA_SIZE     512
#define UART1_TX_DMA_SIZE     512
#define UART1_RX_STREAM_SIZE  512
#define UART1_TX_STREAM_SIZE  1024

static uart_async_t uart1_async;

static uint8_t uart1_rx_dma_buf[UART1_RX_DMA_SIZE]
  __attribute__((section(".dma_buffer"), aligned(32), used));

static uint8_t uart1_tx_dma_buf[UART1_TX_DMA_SIZE]
  __attribute__((section(".dma_buffer"), aligned(32), used));
```

初始化：

```c
exit_code_t uart1_async_port_init(void)
{
  uart_async_config_t cfg = {
    .huart = &huart1,
    .rx_dma_buf = uart1_rx_dma_buf,
    .rx_dma_size = UART1_RX_DMA_SIZE,
    .tx_dma_buf = uart1_tx_dma_buf,
    .tx_dma_size = UART1_TX_DMA_SIZE,
    .rx_stream_size = UART1_RX_STREAM_SIZE,
    .tx_stream_size = UART1_TX_STREAM_SIZE,
    .tx_task_name = "uart1_tx",
    .tx_task_stack_words = 256,
    .tx_task_prio = tskIDLE_PRIORITY + 2,
  };

  return uart_async_init(&uart1_async, &cfg);
}

uart_async_t *uart1_async_port_get(void)
{
  return &uart1_async;
}
```

## 9. Shell 接入

Shell adapter 放在 service 层：

```text
src/service/cli/shell_uart_port.c
src/service/cli/shell_uart_port.h
```

职责：

- 持有 `Shell shell_uart1`。
- 提供 shell read/write 函数。
- 初始化 shell 命令 buffer。
- 不直接调用 HAL UART API。

示例：

```c
static Shell shell_uart1;
static char shell_uart1_cmd_buf[256];
static uart_async_t *shell_uart;

static unsigned short shell_uart_read(char *data, unsigned short len)
{
  return (unsigned short)uart_async_read(shell_uart,
                                         (uint8_t *)data,
                                         len,
                                         portMAX_DELAY);
}

static signed short shell_uart_write(char *data, unsigned short len)
{
  size_t sent = uart_async_write(shell_uart,
                                 (const uint8_t *)data,
                                 len,
                                 pdMS_TO_TICKS(10));
  return (signed short)sent;
}
```

初始化：

```c
exit_code_t shell_uart_port_init(uart_async_t *uart)
{
  shell_uart = uart;
  shell_uart1.read = shell_uart_read;
  shell_uart1.write = shell_uart_write;
  shellInit(&shell_uart1, shell_uart1_cmd_buf, sizeof(shell_uart1_cmd_buf));
  return EXIT_OK;
}

Shell *shell_uart_port_get_shell(void)
{
  return &shell_uart1;
}
```

FreeRTOS 创建 shell task：

```c
uart1_async_port_init();
uart_async_start(uart1_async_port_get());

shell_uart_port_init(uart1_async_port_get());
osThreadNew(shellTask, shell_uart_port_get_shell(), &shellTask_attributes);
```

## 10. Shell 命令导出与 Linker

当前 shell 默认开启 `SHELL_USING_CMD_EXPORT = 1`。GCC 下 `shell.c` 需要 `_shell_command_start` 和 `_shell_command_end`。

linker 需要加入：

```ld
.shellCommand :
{
  . = ALIGN(4);
  _shell_command_start = .;
  KEEP(*(shellCommand))
  KEEP(*(shellCommand*))
  . = ALIGN(4);
  _shell_command_end = .;
} >FLASH
```

位置建议放在 `.rodata` 后或 `.text`/`.rodata` 附近。Shell command 是 const 对象，放 FLASH 合理。

如果不想改 linker，也可以把 `SHELL_USING_CMD_EXPORT` 设为 0，并使用 `shell_cmd_list.c` 静态命令表。但本设计推荐保留 export 模式，避免后续命令注册维护集中表。

## 11. 中断优先级

当前 DMA/USART 中断优先级为 5，符合 FreeRTOS 中断 API 使用要求的常见配置。

约束：

- 调用 `xStreamBufferSendFromISR()`、`xSemaphoreGiveFromISR()` 的中断优先级必须不高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 所允许的范围。
- USART1 IRQ 和 DMA1 Stream IRQ 应保持同一 FreeRTOS-safe 优先级，例如 5。
- 不要在 ISR 中调用阻塞 API。

## 12. Buffer 与内存预算

默认 `.dma_buffer` 使用：

| 模块 | Buffer | 大小 |
| --- | --- | ---: |
| matrix | `spi_temp` | 约 28672 bytes |
| UART1 | RX DMA buffer | 512 bytes |
| UART1 | TX DMA buffer | 512 bytes |
| 合计 | `.dma_buffer` | 约 29696 bytes |

当前 MPU non-cacheable region 是 32KB，所以仍有约 3KB 余量。

注意：

- 如果增大 matrix `MATRIX_MAX_LEDS`，或者增加 UART DMA buffer，需要同步增大 MPU Region 1 和 linker assertion。
- `.dma_buffer` 只放 DMA 直接访问的 buffer。
- FreeRTOS stream buffer 不要放 `.dma_buffer`，否则浪费 non-cacheable 区域。

## 13. 错误与溢出策略

统一策略：不中断系统运行，记录计数，尽可能恢复。

RX：

- RX stream buffer 满：丢弃无法写入的字节，增加 `rx_dropped`。
- UART error：增加 `rx_errors`，abort/restart RX DMA。
- DMA pos 异常：忽略该事件并增加错误计数。

TX：

- TX stream buffer 满：丢弃无法写入的字节，增加 `tx_dropped`。
- `HAL_UART_Transmit_DMA()` 失败：增加 `tx_errors`，当前 chunk 视为失败，TX task 继续处理后续数据。
- TX complete timeout：abort transmit，清理 semaphore，增加 `tx_errors`。

Shell：

- shell 输入丢字节时命令可能不完整，这是可接受的调试口降级行为。
- shell 输出丢字节时只影响日志可读性，不应阻塞核心业务任务。

## 14. 开发步骤

建议按以下顺序实现：

1. 新增 `uart_async.c/.h`
   - 定义 config、ctx、stats。
   - 实现 init/start/stop/read/write。
   - 实现 RX event delta copy。
   - 实现 TX task 和 TX complete semaphore。

2. 新增 USART1 port
   - 定义 USART1 RX/TX DMA buffer，并放 `.dma_buffer`。
   - 配置默认 buffer size 和 TX task 参数。
   - 暴露 `uart1_async_port_init()` 和 `uart1_async_port_get()`。

3. 接入 HAL callbacks
   - 在全局 `HAL_UARTEx_RxEventCallback()`、`HAL_UART_TxCpltCallback()`、`HAL_UART_ErrorCallback()` 中转发到 `uart_async`。
   - 如果已有其它模块实现同名 callback，需要合并为统一分发，不要定义多个同名函数。

4. 修改 shell adapter
   - 新增 `shell_uart_port.c/.h`。
   - Shell read/write 只调用 `uart_async`。
   - 初始化 shell 后创建 `shellTask`。

5. 修正 linker shell command section
   - 添加 `.shellCommand` section。
   - 保持 `SHELL_USING_CMD_EXPORT=1`。
   - 在 `shell_cfg.h` 中 include FreeRTOS tick 所需头文件。

6. 更新 CMake
   - 加入 `uart_async.c`、USART1 port、shell adapter、shell/log 源文件。

7. 验证 `.dma_buffer`
   - 构建后检查 map 文件。
   - 确认 `.dma_buffer` 在 `0x30000000`。
   - 确认 `.dma_buffer <= 32KB`。

## 15. 验证计划

构建验证：

```powershell
cmake --build --preset Debug
```

Map 验证：

- `.dma_buffer` 地址为 `0x30000000`。
- matrix `spi_temp`、USART1 RX DMA buffer、USART1 TX DMA buffer 位于 `.dma_buffer`。
- `_shell_command_start` / `_shell_command_end` 存在。

串口功能验证：

- 上电后 shell 正常显示提示符。
- 输入单字符有响应。
- 输入普通命令并回车，shell 能执行。
- 粘贴超过 256 字节数据，系统不死锁，丢弃计数增加。
- 输出超过 512 字节内容，TX task 能分多次 DMA 发送。
- matrix 刷新同时使用 shell，SPI DMA 与 UART DMA 均正常。

错误恢复验证：

- 暂停/恢复串口工具，确认 RX 能继续接收。
- 快速连续输入，确认 RX circular 不因 IDLE 停止。
- 人为降低 TX timeout 或制造 TX busy，确认 TX task 能 abort 后恢复。

## 16. 关键实现注意事项

- `HAL_UARTEx_ReceiveToIdle_DMA()` 启动后，应确保 RX DMA 是 circular 模式。
- 不要按 Normal DMA 的“接收完重启”思路处理 circular DMA。
- HAL `RxEventCallback` 的 `Size/Pos` 不是新增长度，是当前位置。
- `uart_async_write()` 不应直接启动 DMA；只写 TX stream buffer。
- TX DMA buffer 不能在 DMA busy 时被覆盖。
- ISR 中不得使用阻塞等待。
- 所有 DMA 直接访问的 buffer 必须 32 字节对齐并位于 non-cacheable `.dma_buffer`。
- `.dma_buffer` 总占用必须和 MPU region 大小一致。
- Shell/log 输出不要直接调用 `HAL_UART_Transmit_DMA()`，否则会绕过队列和串行化保护。
