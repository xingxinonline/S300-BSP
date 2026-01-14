/* S300 SRAM boot, no internal flash */
ENTRY(Reset_Handler)

MEMORY
{
  SRAM0 (rwx) : ORIGIN = 0x10000000, LENGTH = 8K
  SRAM1 (rwx) : ORIGIN = 0x20000000, LENGTH = 384K
}

_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
  } > SRAM1

  .data :
  {
    *(.data*)
  } > SRAM1

  .bss :
  {
  _sbss = .;
  __bss_start__ = _sbss;
  *(.bss*)
    *(COMMON)
  _ebss = .;
  __bss_end__ = _ebss;
  } > SRAM1
}
