# H750 Balanced Linker Layout Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Rebalance the STM32H750 memory layout so general runtime data uses AXI SRAM, DTCM is reserved for stack/newlib heap, and FreeRTOS heap is placed in its own application-owned section.

**Architecture:** Keep the startup model simple by continuing to initialize only the standard `.data` and `.bss` ranges, but move those ranges to AXI SRAM. Reserve DTCM for the MSP stack and `_sbrk()` heap, and add explicit NOLOAD sections for RTOS heap and future DMA buffers so cache-sensitive or large allocations stop consuming DTCM by default.

**Tech Stack:** STM32H750, GNU ld linker script, STM32CubeMX-generated startup, FreeRTOS heap_4, CMake

---

### Task 1: Refactor the linker layout

**Files:**
- Modify: `STM32H750XX_FLASH.ld`

**Step 1: Update memory placement**

Move `.data`, `.tdata`, `.tbss`, and `.bss` to `RAM` (`0x24000000`) while keeping `_estack` and `._user_heap_stack` in `DTCMRAM`.

**Step 2: Add explicit reserved sections**

Add a `.freertos_heap (NOLOAD)` section in AXI SRAM and a `.dma_buffer (NOLOAD)` section in `RAM_D2` for future cache-sensitive DMA buffers.

**Step 3: Preserve startup compatibility**

Keep the linker symbols `_sidata`, `_sdata`, `_edata`, `_sbss`, and `_ebss` valid for the generated startup code so no startup assembly changes are required.

**Step 4: Commit**

```bash
git add STM32H750XX_FLASH.ld
git commit -m "refactor: rebalance H750 linker memory layout"
```

### Task 2: Move the FreeRTOS heap into an application-owned section

**Files:**
- Modify: `Core/Inc/FreeRTOSConfig.h`
- Create: `Core/Src/rtos_heap.c`
- Modify: `CMakeLists.txt`

**Step 1: Enable application-owned heap storage**

Set `configAPPLICATION_ALLOCATED_HEAP` to `1` so `heap_4.c` uses an external `ucHeap`.

**Step 2: Define the heap array in application code**

Create `Core/Src/rtos_heap.c` with an 8-byte-aligned `ucHeap[configTOTAL_HEAP_SIZE]` placed in the `.freertos_heap` section.

**Step 3: Add the source file to the build**

Register `Core/Src/rtos_heap.c` in the top-level `CMakeLists.txt`.

**Step 4: Commit**

```bash
git add Core/Inc/FreeRTOSConfig.h Core/Src/rtos_heap.c CMakeLists.txt
git commit -m "refactor: move freertos heap into dedicated section"
```

### Task 3: Verify the new layout

**Files:**
- Inspect: `build/Debug/H7RTOS.map`

**Step 1: Run a fresh build**

Run: `cmake --build --preset Debug`

Expected: build succeeds with exit code `0`

**Step 2: Inspect the map**

Confirm:
- `.data` and `.bss` are in `0x24000000` AXI SRAM
- `._user_heap_stack` remains in `0x20000000` DTCM
- `.freertos_heap` is emitted separately in AXI SRAM
- `.dma_buffer` exists as a reserved NOLOAD section in `RAM_D2`

**Step 3: Record the result**

If verification succeeds, report the exact sections and addresses from the generated `.map` file. If it fails, report the actual linker/build error before claiming success.
