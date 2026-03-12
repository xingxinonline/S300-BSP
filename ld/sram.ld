/* S300 SRAM boot, no internal flash */
ENTRY(Reset_Handler)

MEMORY
{
  SRAM0 (rw) : ORIGIN = 0x10000000, LENGTH = 8K
  SRAM1 (rw) : ORIGIN = 0x20000000, LENGTH = 384K - 256
}

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > SRAM1 :text

  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    . = ALIGN(4);
  } > SRAM1 :text

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > SRAM1 :data

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
    end = __bss_end__;
    _end = __bss_end__;
  } > SRAM1 :data
}
