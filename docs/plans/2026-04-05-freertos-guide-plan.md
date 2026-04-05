# FreeRTOS Guide Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为仓库新增一份中文 FreeRTOS 使用指南，覆盖设计方法、高频 API、全量函数索引和 CMSIS-RTOS2 对照。

**Architecture:** 文档分为工程设计正文和 API 索引附录两层。正文先解释 RTOS 思维模型，再结合本工程的 `main -> MX_FREERTOS_Init -> osKernelStart` 结构给出架构建议，最后追加原生 FreeRTOS V10.3.1 全量公开接口清单和 CMSIS-RTOS2 映射表。

**Tech Stack:** STM32H750, FreeRTOS V10.3.1, CMSIS-RTOS2 wrapper, Markdown

---

### Task 1: Collect API Surface

**Files:**
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/task.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/queue.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/event_groups.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/timers.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/stream_buffer.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/message_buffer.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/croutine.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/include/portable.h`
- Read: `Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.h`

**Step 1:** Extract the public FreeRTOS and CMSIS-RTOS2 APIs that are relevant to application developers.

**Step 2:** Split the interfaces into high-frequency, medium-frequency, low-frequency/internal categories.

### Task 2: Write The Main Guide

**Files:**
- Create: `docs/2026-04-05-freertos-usage-guide.md`
- Reference: `Core/Src/freertos.c`
- Reference: `Core/Inc/FreeRTOSConfig.h`

**Step 1:** Write the overview, version scope, and repo-specific context.

**Step 2:** Write the “RTOS vs bare-metal” and “how to design RTOS applications” sections.

**Step 3:** Write the project-oriented architecture section for STM32H750.

**Step 4:** Write detailed high-frequency API explanations with examples.

**Step 5:** Write the medium/low-frequency API index and CMSIS-RTOS2 mapping table.

### Task 3: Verify Content Coverage

**Files:**
- Review: `docs/2026-04-05-freertos-usage-guide.md`

**Step 1:** Confirm all major public API groups are covered.

**Step 2:** Confirm the document distinguishes task context vs ISR context.

**Step 3:** Confirm the guide gives repo-specific startup and architecture advice instead of generic textbook content.
