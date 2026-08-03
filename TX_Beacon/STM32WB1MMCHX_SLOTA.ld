/*
 * Linker script for TX_Beacon application in OTA Slot A.
 *
 * App is linked at 0x08002800 (page 5, OTA Slot A start).
 * Boot address 0x08000000 holds the separate bootloader binary.
 * Use this script when building with -DOTA_LAYOUT.
 *
 * Flash layout (OTA_LAYOUT, after BLE light stack reflash):
 *   0x08000000  Pages  0-4   Bootloader (10 KB)
 *   0x08002800  Pages  5-46  Slot A app (84 KB) ← this binary
 *   0x08017800  Pages 47-88  Slot B     (84 KB)  download buffer
 *   0x0802C200  Page  89    OTA metadata
 *   0x0802C400  Page  90    Config
 *   0x0802C600  Page  91    Log header
 *   0x0802C800  Pages 92-97  Log data
 *   0x0802E800  Page  98    HW descriptor
 *   0x0802EA00+ Pages 99+   BLE light stack
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM1) + LENGTH(RAM1);
_Min_Heap_Size  = 0x40;
_Min_Stack_Size = 0x800;

MEMORY
{
/* Slot A — pages 5-46, 42 × 2048 = 86016 bytes = 84 KB */
FLASH   (rx)   : ORIGIN = 0x08002800, LENGTH = 84K
RAM1    (xrw)  : ORIGIN = 0x20000008, LENGTH = 0x2FF8
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
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
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

  .preinit_array (READONLY) :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array (READONLY) :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array (READONLY) :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
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

  .ARM.attributes 0       : { *(.ARM.attributes) }
   MAPPING_TABLE (NOLOAD) : { *(MAPPING_TABLE) } >RAM_SHARED
   MB_MEM1 (NOLOAD)       : { *(MB_MEM1) } >RAM_SHARED
   MB_MEM2 (NOLOAD)       : { _sMB_MEM2 = . ; *(MB_MEM2) ; _eMB_MEM2 = . ; } >RAM_SHARED
}
_siMB_MEM2 = _sMB_MEM2;
