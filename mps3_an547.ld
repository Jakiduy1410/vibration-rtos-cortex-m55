/* Linker script for ARM MPS3 AN547 (Cortex-M55)
 * Conforming to Chapter III Architecture Design:
 * - FLASH (ITCM): 512 KB at 0x00000000 (Code, Vector Table, Read-Only Data)
 * - DTCM: 128 KB at 0x20000000 (Double Buffer, FFT Workspace, Critical Task Stacks)
 * - SRAM: 4 MB at 0x21000000 (General Data/BSS, FreeRTOS Heap 64 KB, Logging Task Stack, MSP)
 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
  DTCM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  SRAM  (rwx) : ORIGIN = 0x21000000, LENGTH = 4M
}

__STACK_SIZE = 0x2000; /* 8 KB for MSP (Main Stack Pointer) */
__HEAP_SIZE  = 0x4000; /* 16 KB for libc heap */

ENTRY(Reset_Handler)

SECTIONS
{
  .text :
  {
    KEEP(*(.vectors))
    KEEP(*(.vectors.*))
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    KEEP(*(.eh_frame*))
    . = ALIGN(4);
  } > FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > FLASH

  __exidx_start = .;
  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH
  __exidx_end = .;

  /* Table for newlib/CMSIS startup memory initialization */
  .copy.table :
  {
    . = ALIGN(4);
    __copy_table_start__ = .;
    LONG (__sram_data_load)
    LONG (__data_start__)
    LONG ((__data_end__ - __data_start__) / 4)
    LONG (__dtcm_data_load)
    LONG (__dtcm_data_start__)
    LONG ((__dtcm_data_end__ - __dtcm_data_start__) / 4)
    __copy_table_end__ = .;
  } > FLASH

  .zero.table :
  {
    . = ALIGN(4);
    __zero_table_start__ = .;
    LONG (__bss_start__)
    LONG ((__bss_end__ - __bss_start__) / 4)
    LONG (__dtcm_bss_start__)
    LONG ((__dtcm_bss_end__ - __dtcm_bss_start__) / 4)
    __zero_table_end__ = .;
  } > FLASH

  . = ALIGN(4);
  __etext = .;
  _etext = .;

  __dtcm_data_load = __etext;

  /* DTCM Section (Data Tightly-Coupled Memory: 128 KB at 0x20000000)
   * Holds: 2 KB Double Buffer, 4 KB FFT Workspace, 6 KB Stacks for 3 Critical Tasks
   * Total DTCM footprint: ~13 KB out of 128 KB (conforming to Table 3.3)
   */
  .dtcm_data : AT (__dtcm_data_load)
  {
    . = ALIGN(16);
    __dtcm_data_start__ = .;
    *(.dtcm_data*)
    . = ALIGN(16);
    __dtcm_data_end__ = .;
  } > DTCM

  .dtcm_bss (NOLOAD) :
  {
    . = ALIGN(16);
    __dtcm_bss_start__ = .;
    *(.dtcm_bss*)
    . = ALIGN(16);
    __dtcm_bss_end__ = .;
  } > DTCM

  __sram_data_load = __dtcm_data_load + SIZEOF(.dtcm_data);

  /* SRAM Section (Internal SRAM: 4 MB at 0x21000000)
   * Holds: General .data, .bss, FreeRTOS heap_5 (64 KB), Logging Task Stack, MSP
   */
  .data : AT (__sram_data_load)
  {
    . = ALIGN(4);
    __data_start__ = .;
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    __data_end__ = .;
    _edata = .;
  } > SRAM

  .bss :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
    _ebss = .;
  } > SRAM

  .heap (COPY) :
  {
    . = ALIGN(8);
    __end__ = .;
    PROVIDE(end = .);
    . = . + __HEAP_SIZE;
    . = ALIGN(8);
    __HeapLimit = .;
  } > SRAM

  .stack (ORIGIN(SRAM) + LENGTH(SRAM) - __STACK_SIZE) (COPY) :
  {
    . = ALIGN(8);
    __StackLimit = .;
    . = . + __STACK_SIZE;
    . = ALIGN(8);
    __StackTop = .;
  } > SRAM
  PROVIDE(__stack = __StackTop);
  PROVIDE(__INITIAL_SP = __StackTop);
  PROVIDE(__STACK_LIMIT = __StackLimit);

  ASSERT(__StackLimit >= __HeapLimit, "Error: SRAM overflowed with stack and heap")
}
