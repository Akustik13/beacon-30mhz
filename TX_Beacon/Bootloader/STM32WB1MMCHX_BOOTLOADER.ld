/*
 * Linker script for the OTA bootloader (separate build target).
 *
 * Bootloader occupies pages 0-4 (10 KB) at 0x08000000.
 * It is flashed ONCE via SWD and is NEVER itself a target of OTA.
 * After verifying Slot B and copying it to Slot A, it jumps to 0x08002800.
 */

ENTRY(Bootloader_Reset_Handler)

_estack = ORIGIN(RAM1) + LENGTH(RAM1);
_Min_Heap_Size  = 0x00;   /* no heap in bootloader */
_Min_Stack_Size = 0x400;  /* 1 KB stack is enough  */

MEMORY
{
/* Pages 0-4: 5 × 2048 = 10240 bytes = 10 KB */
FLASH (rx)       : ORIGIN = 0x08000000, LENGTH = 10K
RAM1  (xrw)      : ORIGIN = 0x20000008, LENGTH = 0x2FF8
RAM_SHARED (xrw) : ORIGIN = 0x20030000, LENGTH = 10K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM       (READONLY) :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM1 AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM1

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM1

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
