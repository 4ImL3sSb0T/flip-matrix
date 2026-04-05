# STM32H750 FreeRTOS 使用指南

Date: 2026-04-05

## Scope

本文面向 `H7RTOS` 仓库，基于本地源码中的 FreeRTOS Kernel `V10.3.1` 编写，主线讲原生 FreeRTOS API，同时补充 CMSIS-RTOS2 对照。

主要依据：

- `Core/Src/freertos.c`
- `Core/Inc/FreeRTOSConfig.h`
- `Middlewares/Third_Party/FreeRTOS/Source/include/*.h`
- `Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.h`

本文分两层：

1. 先讲 RTOS 应用应该怎么设计，什么场景该用什么机制。
2. 再按使用频率列出 FreeRTOS 的公开接口，方便查表。

## 1. 本仓库中的 FreeRTOS 位置

这个工程当前通过 CMSIS-RTOS2 包装层使用 FreeRTOS：

- 任务创建在 `Core/Src/freertos.c` 中的 `osThreadNew()`
- 延时使用 `osDelay()`
- 调度器初始化和启动在 `main()` 中通过 `osKernelInitialize()` / `osKernelStart()`

但底层内核仍然是原生 FreeRTOS，配置文件在 `Core/Inc/FreeRTOSConfig.h`。这意味着：

- 设计方法应按 FreeRTOS 思维来做
- 高级排障和性能优化时，应理解原生 FreeRTOS 的任务、队列、通知、定时器、调度器行为
- 如果后续要绕开 CMSIS 封装，直接使用原生 FreeRTOS API 也是完全可行的

当前配置里已经启用了：

- 动态分配与静态分配
- 互斥锁、递归互斥锁、计数信号量
- 软件定时器
- trace facility
- CMSIS-RTOS2 适配层

## 2. 裸机超级循环和 RTOS 逻辑有什么区别

### 2.1 裸机超级循环

裸机常见写法是：

```c
while (1)
{
    poll_uart();
    scan_keys();
    update_control();
    blink_led();
}
```

它的核心特征是：

- 所有逻辑共享一条主执行流
- 谁先执行、谁后执行，完全由代码顺序决定
- “等待事件”通常靠轮询、状态机或全局标志位
- 某一段逻辑变慢，会拖慢整个系统循环周期

优点是简单、可控、调试直观。缺点是随着外设和协议变多，响应延迟和耦合会迅速恶化。

### 2.2 RTOS 逻辑

RTOS 的核心不是“多线程很高级”，而是“把等待独立出来”。

在 RTOS 里：

- 每个任务有自己的栈
- 调度器决定当前谁运行
- 任务大部分时间应该阻塞等待，而不是空转轮询
- 优先级决定抢占关系
- ISR 只做最短路径处理，再把工作交给任务

所以 RTOS 程序更像：

- 中断产生事件
- 任务因事件被唤醒
- 任务处理完后再次阻塞

这和裸机的最大区别是：

- 裸机关注“主循环这一轮执行了什么”
- RTOS 关注“哪个任务因什么事件被唤醒、何时阻塞、谁拥有资源”

### 2.3 两种模型的工程差异

| 维度 | 裸机超级循环 | RTOS |
| --- | --- | --- |
| 主体结构 | 一条主循环 | 多个任务 + 调度器 |
| 等待方式 | 轮询 | 阻塞等待 |
| 响应延迟 | 取决于主循环一圈多久 | 取决于优先级和唤醒路径 |
| 共享资源 | 常靠全局变量 | 必须显式同步 |
| 周期任务 | 手工定时 | `vTaskDelayUntil()` / 软件定时器 |
| 中断设计 | ISR 可能做很多事 | ISR 应尽量短，只做通知 |
| 调试关注点 | 主循环状态机 | 任务状态、栈、优先级、同步关系 |

## 3. RTOS 应用应该怎么设计

### 3.1 先按“事件源”和“资源归属”拆，而不是按函数拆

错误拆法：

- 一个文件一个任务
- 一个外设初始化函数一个任务
- 把所有状态都放到全局变量里，任务互相直接改

正确拆法：

- 谁等待什么事件，就给谁一个任务
- 谁独占某个资源，就让那个任务拥有资源
- 任务之间只通过消息、通知、事件、信号量交互

例如：

- UART 接收完成中断只负责把“收到数据”这件事通知给串口服务任务
- 协议解析任务从队列取帧，不直接碰 DMA 寄存器
- 控制任务按固定周期运行，不在别的任务里被顺手调用

### 3.2 优先级按“时效性”排，不按“重要性”排

常见误区是把“业务上重要”的任务设成最高优先级。实际应该按：

- 是否有硬实时期限
- 是否直接处理 ISR 释放出来的数据
- 是否必须固定周期运行
- 是否只是日志、界面、后台维护

一个实用排序原则：

1. 与中断配合、需要快速清空硬件缓冲的任务
2. 固定周期控制任务
3. 协议处理 / 业务逻辑任务
4. 日志、监控、低优先级维护任务

### 3.3 优先选择阻塞式设计

RTOS 最常见的浪费是：

```c
for (;;)
{
    if (flag)
    {
        do_work();
    }
}
```

这不是 RTOS 思维，只是把裸机轮询搬进了任务。

更好的方式是：

- `xQueueReceive()` 阻塞等消息
- `ulTaskNotifyTake()` 阻塞等通知
- `xSemaphoreTake()` 阻塞等同步条件
- `xEventGroupWaitBits()` 阻塞等多个条件

### 3.4 资源设计规则

- 队列传“数据”
- 信号量传“同步”
- 互斥锁保护“共享资源”
- 任务通知做“一对一轻量唤醒”
- 事件组表达“多位状态条件”
- 软件定时器做“轻量异步超时/周期动作”

不要混用语义。例如：

- 用互斥锁做 ISR 到任务的同步，不合适
- 用二值信号量传复杂数据，不合适
- 用队列传 1bit 事件但没有负载，很多时候不如任务通知

### 3.5 ISR 设计规则

ISR 中只做三件事：

1. 读取/清除必要硬件状态
2. 保存最小必要数据
3. 调用 `FromISR` 接口唤醒任务

不要在 ISR 里：

- 做协议解析
- 做复杂数学运算
- 打印大量日志
- 调用普通任务 API

典型 ISR 模式：

```c
void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 清中断、搬走数据 */
    /* ... */

    vTaskNotifyGiveFromISR(xUartTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### 3.6 周期任务用 `vTaskDelayUntil()`，不是 `vTaskDelay()`

如果你希望任务“每 10ms 执行一次”，用 `vTaskDelay()` 会累计漂移，因为它是“干完再等”。  
如果你希望任务“对齐周期节拍”，用 `vTaskDelayUntil()`。

## 4. 适合本仓库的 RTOS 应用架构

### 4.1 当前启动方式

本仓库当前链路是：

1. `main()` 完成时钟、HAL、GPIO、USART 等基础初始化
2. `osKernelInitialize()`
3. `MX_FREERTOS_Init()` 创建任务和内核对象
4. `osKernelStart()`

这是合理的。建议继续保持：

- 板级初始化留在 `main()`
- 内核对象创建留在 `MX_FREERTOS_Init()`
- 运行期业务初始化放到启动任务里

### 4.2 推荐架构

建议把 RTOS 应用组织成下面这种结构：

```text
main()
  ├─ 时钟 / Cache / HAL / GPIO / UART / DMA 初始化
  ├─ 创建队列、信号量、互斥锁、任务
  └─ 启动调度器

bootstrap_task
  ├─ 启动协议模块
  ├─ 创建业务任务
  ├─ 做一次性自检
  └─ 删除自己

uart_service_task
  ├─ 等待 ISR 通知
  ├─ 搬运或整理接收数据
  └─ 投递到协议队列

protocol_task
  ├─ 阻塞等待帧队列
  ├─ 解析命令
  └─ 分发业务消息

control_task
  ├─ 固定周期运行
  ├─ 读取输入状态
  ├─ 执行控制算法
  └─ 输出执行量

logger_task
  ├─ 低优先级
  └─ 统一串口打印或遥测上报
```

### 4.3 一个实用原则

不要“一个外设一个任务”机械套模板。  
只有在这个外设具备独立等待行为、独立状态机、独立时序要求时，才值得给它单独任务。

例如：

- 一个简单 LED 闪烁，不必单独任务，可以由控制任务或软件定时器处理
- 一个 UART + DMA + 协议解析链路，通常值得至少拆成“接收服务 + 协议处理”

### 4.4 本仓库建议

如果继续沿用 `defaultTask`，建议把它改成 bootstrap task：

- 创建真正的应用任务
- 初始化运行期对象
- 打印启动信息
- 最后 `vTaskDelete(NULL)` 或 `osThreadExit()`

不要把所有业务都长期堆在一个 `defaultTask` 里，否则最终会退化成“披着 RTOS 外壳的超级循环”。

## 5. 高频 API 详解

这一节只讲最常用、最值得熟练掌握的原生 FreeRTOS API。

### 5.1 任务创建与任务主循环

最常用的是：

- `xTaskCreate()`
- `xTaskCreateStatic()`
- `vTaskDelete()`
- `vTaskDelay()`
- `vTaskDelayUntil()`

#### `xTaskCreate()`

用于动态分配任务控制块和栈。

```c
BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,
    const char * const pcName,
    configSTACK_DEPTH_TYPE usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pxCreatedTask
);
```

适用场景：

- 任务数量不大
- 允许使用堆
- 研发阶段快速迭代

#### `xTaskCreateStatic()`

用于静态分配任务控制块和栈，更适合资源可控的 MCU 项目。

建议：

- 关键控制任务优先使用静态创建
- 启动后长期存在的核心任务优先使用静态创建

#### 任务主循环模板

```c
static void ControlTask(void *argument)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        /* 读取输入 */
        /* 更新控制 */
        /* 输出执行量 */

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}
```

说明：

- 周期任务尽量用 `vTaskDelayUntil()`
- 无限循环里不应忙等
- 每轮必须有明确阻塞点或周期点

### 5.2 队列：任务间传数据的主力

高频 API：

- `xQueueCreate()`
- `xQueueCreateStatic()`
- `xQueueSend()`
- `xQueueSendToBack()`
- `xQueueSendToFront()`
- `xQueueReceive()`
- `xQueuePeek()`
- `xQueueSendFromISR()`
- `xQueueReceiveFromISR()`

最典型用法是生产者-消费者。

```c
typedef struct
{
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
} AppMessage_t;

static QueueHandle_t xAppQueue;

static void ProducerTask(void *argument)
{
    AppMessage_t msg = { .cmd = 0x10, .len = 1, .data = {0x55} };

    for (;;)
    {
        xQueueSend(xAppQueue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void ConsumerTask(void *argument)
{
    AppMessage_t msg;

    for (;;)
    {
        if (xQueueReceive(xAppQueue, &msg, portMAX_DELAY) == pdPASS)
        {
            /* 处理消息 */
        }
    }
}
```

设计要点：

- 小对象可直接拷贝进队列
- 大对象更适合传指针，但要明确所有权
- ISR 里只能调用 `FromISR` 版本

什么时候用队列：

- 任务间传结构化数据
- 想把“采集”和“处理”解耦
- 希望消息按到达顺序排队

什么时候别用队列：

- 只想唤醒一个任务，没有数据负载，这时优先考虑任务通知

### 5.3 信号量与互斥锁

高频 API：

- `xSemaphoreCreateBinary()`
- `xSemaphoreTake()`
- `xSemaphoreGive()`
- `xSemaphoreGiveFromISR()`
- `xSemaphoreCreateMutex()`
- `xSemaphoreCreateRecursiveMutex()`
- `xSemaphoreTakeRecursive()`
- `xSemaphoreGiveRecursive()`

#### 二值信号量

用途是“同步”，不是“传数据”。

```c
static SemaphoreHandle_t xRxDoneSem;

void DMA_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 清中断 */
    xSemaphoreGiveFromISR(xRxDoneSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void UartProcessTask(void *argument)
{
    for (;;)
    {
        if (xSemaphoreTake(xRxDoneSem, portMAX_DELAY) == pdTRUE)
        {
            /* 处理 DMA 接收完成后的数据 */
        }
    }
}
```

#### 互斥锁

用途是“保护共享资源”，典型如：

- 串口打印接口
- I2C 总线访问
- 全局配置块

```c
static SemaphoreHandle_t xLogMutex;

void LogPrintf(const char *text)
{
    if (xSemaphoreTake(xLogMutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        /* 串口发送 */
        xSemaphoreGive(xLogMutex);
    }
}
```

关键区别：

- 二值信号量：同步，不带优先级继承
- 互斥锁：保护共享资源，带优先级继承

如果是“任务 A 唤醒任务 B”，优先考虑任务通知。  
如果是“谁都可能访问串口打印”，用互斥锁。

### 5.4 任务通知：一对一最快的同步机制

高频 API：

- `xTaskNotify()`
- `xTaskNotifyFromISR()`
- `xTaskNotifyWait()`
- `ulTaskNotifyTake()`
- `xTaskNotifyAndQuery()`
- `xTaskNotifyAndQueryFromISR()`
- `vTaskNotifyGiveFromISR()`

这是 FreeRTOS 最值得优先掌握的机制之一。

适用场景：

- 一个 ISR 唤醒一个任务
- 一个任务给另一个任务发简单事件
- 不想引入额外队列/信号量对象

ISR 唤醒任务的典型写法：

```c
static TaskHandle_t xUartTaskHandle;

void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 清中断、搬走字节 */

    vTaskNotifyGiveFromISR(xUartTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void UartTask(void *argument)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* 处理收到的数据 */
    }
}
```

为什么高频推荐：

- 比队列和二值信号量更轻
- 不需要单独创建内核对象
- 特别适合 ISR -> task 的一对一唤醒

什么时候不用：

- 一个事件要唤醒多个任务
- 需要排队多个独立消息对象
- 需要显式统计缓冲深度

### 5.5 软件定时器

高频 API：

- `xTimerCreate()`
- `xTimerCreateStatic()`
- `xTimerStart()`
- `xTimerStop()`
- `xTimerReset()`
- `xTimerChangePeriod()`
- `xTimerDelete()`

适合：

- 心跳 LED
- 超时处理
- 空闲后台维护
- 重发超时

不适合：

- 重负载运算
- 长时间阻塞
- 把复杂业务塞进回调

```c
static TimerHandle_t xHeartbeatTimer;

static void HeartbeatTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* 翻转 LED */
}

void AppInit(void)
{
    xHeartbeatTimer = xTimerCreate(
        "heartbeat",
        pdMS_TO_TICKS(500),
        pdTRUE,
        NULL,
        HeartbeatTimerCallback
    );

    xTimerStart(xHeartbeatTimer, 0);
}
```

注意：

- 软件定时器回调运行在 timer daemon task 上下文
- 回调里不要做长阻塞
- 回调里卡住，会拖慢所有软件定时器

## 6. 中频 API：掌握用途，按需用

### 6.1 事件组

常用接口：

- `xEventGroupCreate()`
- `xEventGroupWaitBits()`
- `xEventGroupSetBits()`
- `xEventGroupClearBits()`
- `xEventGroupSync()`

适合：

- 多条件等待
- 多任务 rendezvous
- 多 bit 状态组合

如果你只需要单一事件唤醒，不要上来就用事件组，任务通知更轻。

### 6.2 流缓冲区与消息缓冲区

常用接口：

- `xStreamBufferCreate()`
- `xStreamBufferSend()`
- `xStreamBufferReceive()`
- `xMessageBufferCreate()`
- `xMessageBufferSend()`
- `xMessageBufferReceive()`

选择原则：

- 流缓冲区：连续字节流
- 消息缓冲区：天然带消息边界

UART 字节流、日志流适合 stream buffer。  
变长协议帧适合 message buffer。

### 6.3 临界区与调度器控制

常用接口：

- `taskENTER_CRITICAL()`
- `taskEXIT_CRITICAL()`
- `vTaskSuspendAll()`
- `xTaskResumeAll()`

使用原则：

- 临界区要尽量短
- 临界区不是通用同步工具
- 能用互斥锁、队列、通知解决时，不要靠大临界区硬包

## 7. 设计 RTOS 应用时最常见的错误

### 7.1 每个任务都不阻塞

后果：

- CPU 被空转吃满
- 低优先级任务饿死
- 调试时看起来“系统很忙”，实际没做事

### 7.2 到处共享全局变量

后果：

- race condition
- 任务之间隐式耦合
- 很难定位是谁改坏了状态

正确做法是“资源归属 + 消息传递”。

### 7.3 ISR 做太多逻辑

后果：

- 打断系统实时性
- 增大中断嵌套风险
- 难以复用代码

### 7.4 用错同步机制

常见错法：

- 用互斥锁做事件同步
- 用信号量传复杂对象
- 用队列做单 bit 唤醒
- 用事件组替代所有同步

### 7.5 周期任务写成 `vTaskDelay()`

后果：

- 周期漂移
- 控制回路抖动

固定周期任务应优先用 `vTaskDelayUntil()`。

## 8. 这个仓库里建议怎么落地

### 8.1 启动阶段

建议保留当前模式：

- `main()` 做硬件基础初始化
- `MX_FREERTOS_Init()` 里创建任务和对象
- 启动调度器后由 bootstrap/default task 做业务层初始化

### 8.2 任务组织建议

建议至少区分：

- 通信/外设服务任务
- 协议处理任务
- 周期控制任务
- 日志/监控任务

### 8.3 同步机制优先级建议

推荐优先级：

1. 一对一唤醒：任务通知
2. 传结构化数据：队列
3. 保护共享资源：互斥锁
4. 多条件组合：事件组
5. 字节流/变长消息：stream buffer / message buffer

### 8.4 内存建议

这个仓库已经把 FreeRTOS 堆单独放在 `.freertos_heap` 段。对 MCU 项目建议：

- 关键任务优先静态创建
- 栈大小通过 high-water mark 验证
- 不要盲目把所有对象都动态分配

## 9. 原生 FreeRTOS API 全量索引

下面按使用频率和功能域列出 FreeRTOS V10.3.1 的公开接口。  
说明：

- “高频”指应用开发中应优先掌握
- “中频”指按场景使用
- “低频/内部/移植”指内核诊断、MPU、移植、调试辅助
- 部分接口在头文件里是宏，不是普通函数，本文一并列出

### 9.1 高频：任务与调度

#### 任务创建 / 删除 / 延时

- `xTaskCreate`
- `xTaskCreateStatic`
- `vTaskDelete`
- `vTaskDelay`
- `vTaskDelayUntil`
- `xTaskAbortDelay`

#### 优先级 / 状态 / 挂起恢复

- `uxTaskPriorityGet`
- `uxTaskPriorityGetFromISR`
- `vTaskPrioritySet`
- `eTaskGetState`
- `vTaskSuspend`
- `vTaskResume`
- `xTaskResumeFromISR`

#### 调度器

- `vTaskStartScheduler`
- `vTaskEndScheduler`
- `vTaskSuspendAll`
- `xTaskResumeAll`
- `xTaskGetSchedulerState`

#### Tick / 时间 / 超时

- `xTaskGetTickCount`
- `xTaskGetTickCountFromISR`
- `vTaskSetTimeOutState`
- `xTaskCheckForTimeOut`
- `xTaskCatchUpTicks`
- `vTaskStepTick`

#### 任务查询

- `uxTaskGetNumberOfTasks`
- `pcTaskGetName`
- `xTaskGetHandle`
- `xTaskGetCurrentTaskHandle`
- `xTaskGetIdleTaskHandle`
- `vTaskGetInfo`
- `uxTaskGetStackHighWaterMark`
- `uxTaskGetStackHighWaterMark2`

#### 任务通知

- `xTaskNotify`
- `xTaskNotifyAndQuery`
- `xTaskNotifyFromISR`
- `xTaskNotifyAndQueryFromISR`
- `xTaskNotifyWait`
- `ulTaskNotifyTake`
- `vTaskNotifyGiveFromISR`
- `xTaskNotifyStateClear`

### 9.2 高频：队列

#### 队列创建 / 删除

- `xQueueCreate`
- `xQueueCreateStatic`
- `vQueueDelete`
- `xQueueReset`

#### 发送 / 接收 / 查看

- `xQueueSend`
- `xQueueSendToBack`
- `xQueueSendToFront`
- `xQueueOverwrite`
- `xQueueReceive`
- `xQueuePeek`

#### ISR 版本

- `xQueueSendFromISR`
- `xQueueSendToBackFromISR`
- `xQueueSendToFrontFromISR`
- `xQueueOverwriteFromISR`
- `xQueueReceiveFromISR`
- `xQueuePeekFromISR`

#### 队列状态

- `uxQueueMessagesWaiting`
- `uxQueueSpacesAvailable`
- `xQueueIsQueueEmptyFromISR`
- `xQueueIsQueueFullFromISR`

### 9.3 高频：信号量与互斥锁

#### 二值 / 计数信号量

- `vSemaphoreCreateBinary`
- `xSemaphoreCreateBinary`
- `xSemaphoreCreateBinaryStatic`
- `xSemaphoreCreateCounting`
- `xSemaphoreCreateCountingStatic`
- `xSemaphoreTake`
- `xSemaphoreGive`
- `xSemaphoreGiveFromISR`
- `uxSemaphoreGetCount`
- `uxSemaphoreGetCountFromISR`
- `vSemaphoreDelete`

#### 互斥锁

- `xSemaphoreCreateMutex`
- `xSemaphoreCreateMutexStatic`
- `xSemaphoreCreateRecursiveMutex`
- `xSemaphoreCreateRecursiveMutexStatic`
- `xSemaphoreTakeRecursive`
- `xSemaphoreGiveRecursive`
- `xSemaphoreGetMutexHolder`
- `xSemaphoreGetMutexHolderFromISR`

### 9.4 高频：软件定时器

- `xTimerCreate`
- `xTimerCreateStatic`
- `xTimerStart`
- `xTimerStop`
- `xTimerReset`
- `xTimerChangePeriod`
- `xTimerDelete`
- `xTimerIsTimerActive`
- `pvTimerGetTimerID`
- `vTimerSetTimerID`
- `pcTimerGetName`

#### ISR 版本

- `xTimerStartFromISR`
- `xTimerStopFromISR`
- `xTimerResetFromISR`
- `xTimerChangePeriodFromISR`

### 9.5 中频：事件组

- `xEventGroupCreate`
- `xEventGroupCreateStatic`
- `xEventGroupWaitBits`
- `xEventGroupSetBits`
- `xEventGroupClearBits`
- `xEventGroupSync`
- `xEventGroupGetBits`
- `xEventGroupGetBitsFromISR`
- `xEventGroupSetBitsFromISR`
- `xEventGroupClearBitsFromISR`
- `vEventGroupDelete`
- `uxEventGroupGetNumber`
- `vEventGroupSetNumber`

### 9.6 中频：流缓冲区

- `xStreamBufferCreate`
- `xStreamBufferCreateStatic`
- `xStreamBufferSend`
- `xStreamBufferSendFromISR`
- `xStreamBufferReceive`
- `xStreamBufferReceiveFromISR`
- `vStreamBufferDelete`
- `xStreamBufferIsFull`
- `xStreamBufferIsEmpty`
- `xStreamBufferReset`
- `xStreamBufferSpacesAvailable`
- `xStreamBufferBytesAvailable`
- `xStreamBufferSetTriggerLevel`
- `xStreamBufferSendCompletedFromISR`
- `xStreamBufferReceiveCompletedFromISR`
- `xStreamBufferNextMessageLengthBytes`
- `vStreamBufferSetStreamBufferNumber`
- `uxStreamBufferGetStreamBufferNumber`

### 9.7 中频：消息缓冲区

- `xMessageBufferCreate`
- `xMessageBufferCreateStatic`
- `xMessageBufferSend`
- `xMessageBufferSendFromISR`
- `xMessageBufferReceive`
- `xMessageBufferReceiveFromISR`
- `vMessageBufferDelete`
- `xMessageBufferIsFull`
- `xMessageBufferIsEmpty`
- `xMessageBufferReset`
- `xMessageBufferSpaceAvailable`
- `xMessageBufferNextLengthBytes`
- `xMessageBufferSendCompletedFromISR`
- `xMessageBufferReceiveCompletedFromISR`

### 9.8 中频：队列集与注册表

- `xQueueCreateSet`
- `xQueueAddToSet`
- `xQueueRemoveFromSet`
- `xQueueSelectFromSet`
- `xQueueSelectFromSetFromISR`
- `vQueueAddToRegistry`
- `vQueueUnregisterQueue`
- `pcQueueGetName`

### 9.9 中频：任务标签、TLS、统计

- `vTaskSetApplicationTaskTag`
- `xTaskGetApplicationTaskTag`
- `xTaskGetApplicationTaskTagFromISR`
- `vTaskSetThreadLocalStoragePointer`
- `pvTaskGetThreadLocalStoragePointer`
- `xTaskCallApplicationTaskHook`
- `uxTaskGetSystemState`
- `vTaskList`
- `vTaskGetRunTimeStats`
- `uxTaskGetTaskNumber`
- `vTaskSetTaskNumber`

### 9.10 低频：临界区、优先级继承、内核内部辅助

- `taskYIELD`
- `taskENTER_CRITICAL`
- `taskEXIT_CRITICAL`
- `taskENTER_CRITICAL_FROM_ISR`
- `taskEXIT_CRITICAL_FROM_ISR`
- `taskDISABLE_INTERRUPTS`
- `taskENABLE_INTERRUPTS`
- `vTaskMissedYield`
- `xTaskPriorityInherit`
- `xTaskPriorityDisinherit`
- `vTaskPriorityDisinheritAfterTimeout`
- `pvTaskIncrementMutexHeldCount`
- `xTaskIncrementTick`
- `vTaskPlaceOnEventList`
- `vTaskPlaceOnUnorderedEventList`
- `vTaskPlaceOnEventListRestricted`
- `xTaskRemoveFromEventList`
- `vTaskRemoveFromUnorderedEventList`
- `uxTaskResetEventItemValue`

### 9.11 低频：软件定时器高级接口

- `xTimerGetTimerDaemonTaskHandle`
- `xTimerPendFunctionCall`
- `xTimerPendFunctionCallFromISR`
- `vTimerSetReloadMode`
- `uxTimerGetReloadMode`
- `xTimerGetPeriod`
- `xTimerGetExpiryTime`
- `xTimerCreateTimerTask`
- `xTimerGenericCommand`
- `vTimerSetTimerNumber`
- `uxTimerGetTimerNumber`

### 9.12 低频：队列/信号量底层泛型接口

- `xQueueGenericCreate`
- `xQueueGenericCreateStatic`
- `xQueueGenericSend`
- `xQueueGenericSendFromISR`
- `xQueueGenericReset`
- `xQueueSemaphoreTake`
- `xQueueGetMutexHolder`
- `xQueueGetMutexHolderFromISR`
- `xQueueCreateMutex`
- `xQueueCreateMutexStatic`
- `xQueueCreateCountingSemaphore`
- `xQueueCreateCountingSemaphoreStatic`
- `xQueueTakeMutexRecursive`
- `xQueueGiveMutexRecursive`
- `vQueueDelete`
- `vQueueAddToRegistry`
- `vQueueUnregisterQueue`
- `vQueueWaitForMessageRestricted`
- `uxQueueGetQueueNumber`
- `vQueueSetQueueNumber`

### 9.13 低频：协程接口

FreeRTOS 协程是历史特性，新项目通常不建议继续使用。

- `xCoRoutineCreate`
- `vCoRoutineSchedule`
- `vCoRoutineAddToDelayedList`
- `xCoRoutineRemoveFromEventList`
- `xQueueCRSend`
- `xQueueCRReceive`
- `xQueueCRSendFromISR`
- `xQueueCRReceiveFromISR`

### 9.14 低频：MPU 任务接口

如果使用 MPU 保护任务，还会有一套 `MPU_` 前缀接口，例如：

- `MPU_xTaskCreate`
- `MPU_xTaskCreateStatic`
- `MPU_xTaskCreateRestricted`
- `MPU_xTaskCreateRestrictedStatic`
- `MPU_vTaskAllocateMPURegions`
- `MPU_vTaskDelete`
- `MPU_vTaskDelay`
- `MPU_vTaskDelayUntil`
- `MPU_xTaskAbortDelay`
- `MPU_uxTaskPriorityGet`
- `MPU_vTaskPrioritySet`
- `MPU_vTaskSuspend`
- `MPU_vTaskResume`
- `MPU_vTaskStartScheduler`
- `MPU_vTaskSuspendAll`
- `MPU_xTaskResumeAll`
- `MPU_xTaskGetTickCount`
- `MPU_uxTaskGetNumberOfTasks`
- `MPU_xTaskGetHandle`
- `MPU_uxTaskGetStackHighWaterMark`
- `MPU_vTaskSetApplicationTaskTag`
- `MPU_vTaskSetThreadLocalStoragePointer`
- `MPU_xTaskCallApplicationTaskHook`
- `MPU_xTaskGetIdleTaskHandle`
- `MPU_uxTaskGetSystemState`
- `MPU_vTaskList`
- `MPU_vTaskGetRunTimeStats`
- `MPU_xTaskGenericNotify`
- `MPU_xTaskNotifyWait`
- `MPU_xTaskNotifyStateClear`
- `MPU_xTaskIncrementTick`
- `MPU_xTaskGetCurrentTaskHandle`
- `MPU_vTaskSetTimeOutState`
- `MPU_xTaskCheckForTimeOut`
- `MPU_vTaskMissedYield`
- `MPU_xTaskGetSchedulerState`
- `MPU_xTaskCatchUpTicks`
- `MPU_xQueueGenericSend`
- `MPU_xQueueReceive`
- `MPU_xQueuePeek`
- `MPU_xQueueSemaphoreTake`
- `MPU_uxQueueMessagesWaiting`
- `MPU_uxQueueSpacesAvailable`
- `MPU_vQueueDelete`
- `MPU_xQueueCreateMutex`
- `MPU_xQueueCreateMutexStatic`
- `MPU_xQueueCreateCountingSemaphore`
- `MPU_xQueueCreateCountingSemaphoreStatic`
- `MPU_xQueueGetMutexHolder`
- `MPU_xQueueTakeMutexRecursive`
- `MPU_xQueueGiveMutexRecursive`
- `MPU_vQueueAddToRegistry`
- `MPU_vQueueUnregisterQueue`
- `MPU_xQueueGenericCreate`
- `MPU_xQueueGenericCreateStatic`
- `MPU_xQueueAddToSet`
- `MPU_xQueueRemoveFromSet`
- `MPU_xQueueSelectFromSet`
- `MPU_xQueuePeekFromISR`
- `MPU_xQueueReceiveFromISR`
- `MPU_xQueueGenericSendFromISR`
- `MPU_xQueueGiveFromISR`
- `MPU_xQueueIsQueueEmptyFromISR`
- `MPU_xQueueIsQueueFullFromISR`
- `MPU_xQueueCRSendFromISR`
- `MPU_xQueueCRReceiveFromISR`
- `MPU_xTimerCreate`
- `MPU_xTimerCreateStatic`
- `MPU_pvTimerGetTimerID`
- `MPU_vTimerSetTimerID`
- `MPU_xTimerIsTimerActive`
- `MPU_xTimerGetTimerDaemonTaskHandle`
- `MPU_xTimerPendFunctionCall`
- `MPU_vTimerSetReloadMode`
- `MPU_uxTimerGetReloadMode`
- `MPU_xTimerGetPeriod`
- `MPU_xTimerGetExpiryTime`
- `MPU_xTimerCreateTimerTask`
- `MPU_xTimerGenericCommand`
- `MPU_xEventGroupCreate`
- `MPU_xEventGroupCreateStatic`
- `MPU_xEventGroupWaitBits`
- `MPU_xEventGroupClearBits`
- `MPU_xEventGroupSetBits`
- `MPU_xEventGroupSync`
- `MPU_vEventGroupDelete`
- `MPU_uxEventGroupGetNumber`
- `MPU_xStreamBufferSend`
- `MPU_xStreamBufferReceive`

这些接口对普通应用开发并不高频，知道它们存在即可。

### 9.15 低频：堆与移植层接口

- `pvPortMalloc`
- `vPortFree`
- `vPortInitialiseBlocks`
- `xPortGetFreeHeapSize`
- `xPortGetMinimumEverFreeHeapSize`
- `vPortDefineHeapRegions`
- `vPortGetHeapStats`
- `xPortStartScheduler`
- `vPortEndScheduler`
- `vPortStoreTaskMPUSettings`

## 10. CMSIS-RTOS2 对照表

本仓库当前直接使用的是 CMSIS-RTOS2 包装层，因此下面给出常用映射。

| 原生 FreeRTOS | CMSIS-RTOS2 | 说明 |
| --- | --- | --- |
| `xTaskCreate` / `xTaskCreateStatic` | `osThreadNew` | CMSIS 把任务称为 thread |
| `vTaskDelete(NULL)` | `osThreadExit` / `osThreadTerminate` | 自删与他删分开理解 |
| `vTaskDelay` | `osDelay` | 相对延时 |
| `vTaskDelayUntil` | `osDelayUntil` | 固定周期任务更适合 |
| `vTaskPrioritySet` / `uxTaskPriorityGet` | `osThreadSetPriority` / `osThreadGetPriority` | 优先级取值体系不同 |
| `taskYIELD()` | `osThreadYield` | 主动让出 CPU |
| `xQueueCreate` | `osMessageQueueNew` | CMSIS 没有“Queue”命名，统一叫消息队列 |
| `xQueueSend` / `xQueueReceive` | `osMessageQueuePut` / `osMessageQueueGet` | 主要一一对应 |
| `xSemaphoreCreateBinary` / `xSemaphoreCreateCounting` | `osSemaphoreNew` | CMSIS 用统一信号量构造 |
| `xSemaphoreTake` / `xSemaphoreGive` | `osSemaphoreAcquire` / `osSemaphoreRelease` | |
| `xSemaphoreCreateMutex` | `osMutexNew` | 互斥锁与信号量分开建模 |
| `xSemaphoreTake` / `xSemaphoreGive` for mutex | `osMutexAcquire` / `osMutexRelease` | 语义更清晰 |
| `xEventGroupSetBits` / `xEventGroupWaitBits` | `osEventFlagsSet` / `osEventFlagsWait` | 事件组最接近 event flags |
| `xTimerCreate` | `osTimerNew` | |
| `xTimerStart` / `xTimerStop` | `osTimerStart` / `osTimerStop` | |
| `xTaskGetTickCount` | `osKernelGetTickCount` | |
| `xTaskGetSchedulerState` | `osKernelGetState` | 粒度不同，但功能接近 |
| `xTaskNotify*` | 无直接等价 | CMSIS 没有原生任务通知这一层 |
| `xStreamBuffer*` | 无直接等价 | 需直接使用原生 FreeRTOS |
| `xMessageBuffer*` | 无直接等价 | 需直接使用原生 FreeRTOS |
| `xQueueCreateSet*` | 无直接等价 | CMSIS 不提供 queue set |

结论很简单：

- 如果只是一般任务、队列、信号量、互斥锁、定时器，CMSIS-RTOS2 足够用
- 如果你要做高性能唤醒、细粒度同步、stream/message buffer、queue set，直接上原生 FreeRTOS 更合适

## 11. 最后给这个项目的建议

如果这个仓库后面会继续做较完整的嵌入式应用，建议遵守下面几条：

1. `defaultTask` 只做 bootstrap，不要变成万能业务任务。
2. ISR 只做最小工作量，并统一使用 `FromISR` API。
3. 一对一唤醒优先用任务通知，不要滥用二值信号量。
4. 固定周期控制逻辑统一改用 `vTaskDelayUntil()` 思维。
5. 日志打印统一收口，不要多个任务同时直接抢串口。
6. 通过 `uxTaskGetStackHighWaterMark()` 持续核对各任务栈深度。
7. 对核心任务优先考虑静态创建，对非核心对象再使用动态分配。

如果只记住一句话：

RTOS 不是“把裸机代码拆成多个 while(1)”；RTOS 是“让每个任务只在自己真正该运行的时候被唤醒”。
