# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Requires `arm-none-eabi-` toolchain on PATH. Uses CMake + Ninja.

```powershell
# Configure and build
cmake --preset Debug
cmake --build --preset Debug

cmake --preset Release
cmake --build --preset Release
```

Output: `build/Debug/H7RTOS.elf` (Debug, `-O0 -g3`) or `build/Release/H7RTOS.elf` (Release, `-Os -g0`). The linker also emits a `.map` file and prints memory usage summary.

Re-run configure (`cmake --preset`) after any changes to `H7RTOS.ioc`, `STM32H750XX_FLASH.ld`, `cmake/`, or toolchain paths.

Agent-driven CMake builds are unreliable in this repo — verify manually.

## Architecture

**Target:** STM32H750VBTx (Cortex-M7, 128KB FLASH, 1MB SRAM across 5 banks).

### Memory Layout

The linker script (`STM32H750XX_FLASH.ld`) uses a multi-bank balancing strategy:

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| ITCM | `0x00000000` | 64KB | Unused; reserved for future hot code |
| FLASH | `0x08000000` | 128KB | Code, rodata, vector table, `.data` init image |
| DTCM | `0x20000000` | 128KB | MSP stack top, newlib heap reservation (512B) |
| AXI SRAM | `0x24000000` | 512KB | `.data`, `.bss`, FreeRTOS heap (64KB) — main runtime RAM |
| D2 SRAM | `0x30000000` | 288KB | Reserved `.dma_buffer` section for future DMA |
| D3 SRAM | `0x38000000` | 64KB | Unused |

The FreeRTOS heap is isolated in its own `.freertos_heap` section at `0x24000e68`, not mixed into general `.bss`. Use `__attribute__((section(".dma_buffer"), aligned(32)))` for DMA data.

### Code Organization Tiers

1. **`Core/`** — CubeMX-generated HAL code. Contains `main.c`, `freertos.c`, `gpio.c`, `usart.c`, interrupt handlers, system init, `FreeRTOSConfig.h`. Custom logic must go inside `/* USER CODE BEGIN ... */` / `/* USER CODE END ... */` markers or CubeMX regeneration will overwrite it.

2. **`Drivers/` + `Middlewares/`** — Vendor code: STM32 HAL, CMSIS, CMSIS-DSP, FreeRTOS V10.3.1. Treat as third-party; avoid editing.

3. **`src/`** — Project application code (not CubeMX-managed):

   - **`src/bsp/`** — Board support package drivers:
     - `uart/` — Shell UART port layer (`shell_port.c`)
     - `ws2812b/` — WS2812B LED driver with interface abstraction
   - **`src/service/`** — Application service modules:
     - `flip/` — FLIP fluid dynamics simulation (particle+grid solver)
     - `imu/` — IMU sensor service with Madgwick AHRS attitude estimation
     - `cli/` — Letter-shell based command-line interface with log system
     - `tools/` — Common utilities: `vec_math` (3D vector/rotation math), `common_def`

### RTOS Architecture

Uses FreeRTOS V10.3.1 via CMSIS-RTOS2 wrapper (`cmsis_os2.h`). Key config (in `FreeRTOSConfig.h`):
- `configTICK_RATE_HZ = 1000`
- `configMAX_PRIORITIES = 56`
- `configTOTAL_HEAP_SIZE = 65535` (64KB in dedicated `.freertos_heap` section)
- Heap 4 allocator, static+dynamic allocation enabled
- Software timers, mutexes, recursive mutexes, counting semaphores enabled

Startup flow: `main()` → hardware init → `osKernelInitialize()` → `MX_FREERTOS_Init()` (create tasks/objects) → `osKernelStart()`. A single `defaultTask` is the bootstrap task.

Prefer raw FreeRTOS API over CMSIS-RTOS2 when you need task notifications, stream/message buffers, or queue sets — CMSIS has no equivalent for these.

### CubeMX Regeneration Safety

`main.c` and `freertos.c` contain CubeMX markers. Custom code between markers survives regeneration. The `src/` directory is outside CubeMX's scope entirely — libraries, drivers, and services there are never touched by code generation.

New user C sources must be added to the `USER_SOURCES` list or `target_sources()` in the root `CMakeLists.txt` (the CubeMX-generated `cmake/stm32cubemx/CMakeLists.txt` is regenerated and cannot be edited).

## Coding Style

- 2-space indentation, K&R braces, `snake_case` for functions and locals
- File names match the module/peripheral (`gpio.c`, `usart.h`, `vec_math.c`)
- No comments needed for self-documenting code; use Doxygen style (`@brief`, `@param`, `@retval`) for public API headers
- Keep CubeMX `USER CODE` markers intact

## Commit Convention

`type(scope): short summary` — scopes seen in history: `main`, `memory`, `freertos`, `cmake`, `project`, `flip`, `simulation`, `service`.
