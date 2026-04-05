# Repository Guidelines

## Project Structure & Module Organization
`Core/Inc` and `Core/Src` contain the application code generated from STM32CubeMX plus user firmware such as `main.c`, `freertos.c`, and `rtos_heap.c`. `Drivers/` and `Middlewares/` hold vendor HAL, CMSIS, DSP, and FreeRTOS sources; treat them as third-party code unless a change is required upstream. Build logic lives in [CMakeLists.txt](/D:/Project/H750/H7RTOS/CMakeLists.txt), [CMakePresets.json](/D:/Project/H750/H7RTOS/CMakePresets.json), and `cmake/`. Hardware configuration is defined by `H7RTOS.ioc`, `STM32H750XX_FLASH.ld`, and `startup_stm32h750xx.s`. Keep design notes and reports in `docs/` and `docs/plans/`.

## Build, Test, and Development Commands
Use the GNU Arm Embedded toolchain (`arm-none-eabi-*`) on `PATH`.

```powershell
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
cmake --build --preset Release
```

`Debug` builds to `build/Debug/H7RTOS.elf` with symbols; `Release` uses size-oriented flags. Re-run `cmake --preset <Preset>` after `.ioc`, linker, or toolchain changes.
The current agent-driven CMake build flow is known to be unreliable in this repository, so final verification should be done by running the build manually on your machine.

## Coding Style & Naming Conventions
Follow the existing STM32/CubeMX C style: 2-space indentation, K&R braces, and `snake_case` for functions and locals. Keep file names aligned with peripheral or module names (`gpio.c`, `usart.h`). Preserve CubeMX markers such as `/* USER CODE BEGIN ... */`; place custom logic inside those regions so regeneration does not overwrite it. Prefer small, hardware-focused changes in `Core/` over editing vendor sources in `Drivers/` or `Middlewares/`.

## Testing Guidelines
There is no top-level unit-test suite or coverage gate for application code. Validate contributions by manually building at least the `Debug` preset and, for runtime changes, smoke-testing on STM32H750 hardware. CMSIS-DSP test sources under `Drivers/CMSIS/DSP/` are upstream assets, not the project’s main test harness.

## Commit & Pull Request Guidelines
Recent history uses Conventional Commit prefixes such as `fix(main): ...`, `refactor(memory): ...`, and `chore(project): ...`. Keep the format `type(scope): short summary`; concise Chinese or English summaries are both already present in history. PRs should explain the hardware-facing impact, list the build preset(s) used for verification, and include screenshots, logs, or memory-map deltas when touching startup, linker, clock, or RTOS allocation code.
