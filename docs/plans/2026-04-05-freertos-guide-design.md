# FreeRTOS 使用指南设计稿

Date: 2026-04-05

## Goal

为 `H7RTOS` 仓库补充一份面向 STM32 嵌入式开发的 FreeRTOS 使用指南。主文档以原生 FreeRTOS V10.3.1 API 为主，补充 CMSIS-RTOS2 对照，并结合本工程当前的启动与任务组织方式给出架构建议。

## Scope

文档需要覆盖：

- RTOS 与裸机超级循环的逻辑差异
- RTOS 应用的任务划分、资源划分、同步策略、启动方式
- 适合本仓库的 RTOS 应用架构建议
- 高频 API 的详细说明、注意事项和例子
- FreeRTOS 公共 API 的全量分类索引
- 原生 FreeRTOS 与 CMSIS-RTOS2 的常见映射关系

## Approach

采用“正文 + 附录索引”结构：

1. 正文优先解决工程设计问题，帮助开发者知道什么时候该用任务、队列、通知、互斥锁、软件定时器。
2. 附录按使用频率和功能域列出 API，便于查表。
3. 文中显式说明本仓库当前使用 `cmsis_os2.c` 包装层，但内核本体是 FreeRTOS V10.3.1。

## Source Basis

主要依据以下本地文件：

- `Core/Src/freertos.c`
- `Core/Inc/FreeRTOSConfig.h`
- `Middlewares/Third_Party/FreeRTOS/Source/include/*.h`
- `Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.h`

## Output

生成主文档：

- `docs/2026-04-05-freertos-usage-guide.md`

文档风格保持仓库现有 `docs/` 目录的技术说明风格，强调工程落地而不是纯 API 罗列。
