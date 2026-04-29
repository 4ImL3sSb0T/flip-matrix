# STM32H750 Current Memory Layout Report

Date: 2026-04-05

## Scope

This report describes the current memory layout of the `H7RTOS` project after the balanced linker adjustment.

Sources used:
- `STM32H750XX_FLASH.ld`
- `build/Debug/H7RTOS.map`
- `build/Debug/H7RTOS.elf`

The addresses and sizes below are based on the current `Debug` build artifacts, not only on linker intent.

## 1. Physical Memory Regions

The linker script defines these memory regions:

| Region | Start | Size | Purpose in current design |
| --- | --- | --- | --- |
| `ITCMRAM` | `0x00000000` | `64 KB` | Reserved for future hot code / ramfunc, currently unused |
| `FLASH` | `0x08000000` | `128 KB` | Vector table, code, constants, flash load image for initialized RAM sections |
| `DTCMRAM` | `0x20000000` | `128 KB` | Main stack top region and reserved newlib heap + MSP stack block |
| `RAM` | `0x24000000` | `512 KB` | Main runtime RAM: `.data`, `.bss`, FreeRTOS heap |
| `RAM_D2` | `0x30000000` | `288 KB` | Reserved for future DMA/non-cacheable buffers |
| `RAM_D3` | `0x38000000` | `64 KB` | Currently unused |

## 2. Current Section Placement

Key sections from `build/Debug/H7RTOS.map`:

| Section | Address | Size | Meaning |
| --- | --- | --- | --- |
| `.isr_vector` | `0x08000000` | `0x298` | Interrupt vector table |
| `.text` | `0x080002a0` | `0x981c` | Executable code |
| `.rodata` | `0x08009abc` | `0x80` | Read-only constants |
| `.data` | `0x24000000` | `0x14` | Initialized writable data in AXI SRAM |
| `.tdata` | `0x24000014` | `0x0` | TLS init data, currently unused |
| `.tbss` | `0x24000014` | `0x0` | TLS zero-init data, currently unused |
| `.bss` | `0x24000014` | `0xe54` | Zero-init globals in AXI SRAM |
| `.freertos_heap` | `0x24000e68` | `0x10000` | Dedicated FreeRTOS heap |
| `.dma_buffer` | `0x30000000` | about `0x7000` for default matrix config | D2 DMA section, currently used by matrix SPI TX buffer |
| `._user_heap_stack` | `0x20000000` | `0x600` | Reserved newlib heap + MSP stack block in DTCM |

## 3. Runtime Memory Structure

### 3.1 FLASH

FLASH currently holds:
- Vector table
- Program code
- Read-only constants
- Load image for `.data`

This means initialized RAM variables are copied from FLASH into `RAM` during startup.

### 3.2 AXI SRAM (`RAM`, `0x24000000`)

AXI SRAM is now the main runtime memory bank.

Current layout:

```text
0x24000000  .data           0x14
0x24000014  .bss            0x0e54
0x24000e68  .freertos_heap  0x10000
```

Important symbols:
- `_sdata = 0x24000000`
- `_edata = 0x24000014`
- `_sbss  = 0x24000014`
- `_ebss  = 0x24000e68`
- `ucHeap = 0x24000e68`

Interpretation:
- Normal global/static variables now live in AXI SRAM instead of DTCM
- The FreeRTOS heap is no longer mixed into ordinary `.bss`
- This leaves much more DTCM headroom and makes future RAM growth easier to manage

### 3.3 DTCM (`0x20000000`)

DTCM is now reserved mainly for stack-related usage and the small newlib heap reservation.

Current reserved block:

```text
0x20000000  _end / end
0x20000200  after _Min_Heap_Size   (0x200 = 512 B)
0x20000600  after _Min_Stack_Size  (0x400 = 1024 B)
0x20020000  _estack
```

Meaning:
- `._user_heap_stack` reserves `0x600` bytes at the start of DTCM
- `_Min_Heap_Size = 0x200` reserves 512 bytes for `_sbrk()` / newlib heap
- `_Min_Stack_Size = 0x400` reserves 1024 bytes as linker-checked MSP stack margin
- `_estack = 0x20020000` is the top of DTCM and the initial stack pointer

Note:
- The linker reservation is only a minimum safety reservation, not the full runtime stack consumption
- Interrupts and early startup still use the MSP from DTCM top downward

### 3.4 D2 SRAM (`RAM_D2`, `0x30000000`)

`RAM_D2` contains the reserved `.dma_buffer` section:

```text
0x30000000  .dma_buffer
```

This section is used for:
- DMA RX/TX buffers
- Ethernet descriptors
- ADC/UART/SPI DMA working buffers
- Other data that may need explicit cache policy handling

The matrix service places its SPI TX encoding buffer in this region with 32-byte alignment. The current MPU configuration covers the first 32KB at `0x30000000` as non-cacheable memory, and the linker asserts that `.dma_buffer` does not exceed that MPU-covered range.

### 3.5 D3 SRAM and ITCM

Both are currently unused:
- `RAM_D3`
- `ITCMRAM`

They remain available for:
- backup / low-speed service data
- mailbox/state blocks
- tightly coupled hot functions if `.RamFunc` is later split into ITCM

## 4. Current Size Summary

From `arm-none-eabi-size build/Debug/H7RTOS.elf`:

| Field | Size |
| --- | --- |
| `text` | `39748` bytes |
| `data` | `20` bytes |
| `bss` | `70740` bytes |
| total (`dec`) | `110508` bytes |

Interpretation:
- Code footprint is currently modest relative to `128 KB` FLASH
- Writable RAM usage is dominated by `.bss` and the dedicated `FreeRTOS` heap
- The largest deliberate RAM consumer is `ucHeap` in `.freertos_heap` with `64 KB`

## 5. Startup Behavior

The startup file still uses the standard linker symbols:
- `_sidata`
- `_sdata`
- `_edata`
- `_sbss`
- `_ebss`

So startup flow remains conventional:

1. Copy `.data` from FLASH load address into AXI SRAM
2. Zero `.bss` in AXI SRAM
3. Leave `.freertos_heap` and `.dma_buffer` as `NOLOAD`
4. Start with MSP at `_estack` in DTCM

This keeps the generated startup assembly compatible while changing only the destination memory bank.

## 6. Why This Layout Is Better Than The Original

Compared with the previous layout that placed nearly everything in `DTCMRAM`, the current structure improves:

- Capacity: large global data no longer competes with stack space inside 128 KB DTCM
- Maintainability: FreeRTOS heap is isolated in its own section and easy to inspect in the map file
- Extensibility: `RAM_D2` is already reserved for future DMA/non-cacheable usage
- H7 fit: the multi-bank SRAM structure is now reflected in the linker design instead of being mostly unused

## 7. Practical Guidance For Future Development

Recommended placement policy going forward:

- Put ordinary globals/statics in default sections and let them land in `RAM`
- Put RTOS dynamic allocations into the existing `FreeRTOS` heap section
- Put DMA-related arrays into a custom section such as:

```c
__attribute__((section(".dma_buffer"), aligned(32)))
```

- Keep the MPU non-cacheable region size and the linker `.dma_buffer` size assertion in sync
- Consider using `ITCMRAM` only for truly hot functions after measurement
- Keep DTCM focused on stack, fast scratch data, or a few latency-sensitive objects

## 8. Current Bottom Line

The project now has this effective runtime structure:

```text
FLASH    -> vector table, code, rodata, RAM init image
DTCM     -> initial MSP top, linker-reserved newlib heap + stack margin
AXI SRAM -> .data + .bss + dedicated FreeRTOS heap
D2 SRAM  -> reserved DMA buffer section
D3 SRAM  -> unused
ITCM     -> unused
```

This is a balanced STM32H750 layout and is a reasonable base for adding peripherals, DMA, and larger RTOS workloads.
