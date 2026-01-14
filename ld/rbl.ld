/*
 * S300 RBL链接脚本
 * RBL运行在SRAM中，由S300 ROMBOOT加载
 * 目标地址：0x20000000 (SRAM)
 * Flash位置：0x100-0x8000
 */

/* 内存布局 */
MEMORY
{
  /* RBL运行时内存 - SRAM */
  SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
  
  /* RBL在Flash中的位置 (仅用于生成bin文件) */
  FLASH (rx) : ORIGIN = 0x08000100, LENGTH = 32512  /* 256字节给Header，剩余32256字节 */
}

/* 栈顶地址 */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);

/* 入口点 */
ENTRY(Reset_Handler)

/* 段定义 */
SECTIONS
{
  /* 中断向量表 */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >SRAM

  /* 代码段 */
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
  } >SRAM

  /* 只读数据段 */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >SRAM

  /* ARM异常处理 */
  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >SRAM
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >SRAM

  /* 初始化数据段 */
  .data : 
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)

    . = ALIGN(4);
    _edata = .;
  } >SRAM

  /* 未初始化数据段 */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >SRAM

  /* 用户堆栈检查 */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + 0x400;  /* 1KB堆 */
    . = . + 0x800;  /* 2KB栈 */
    . = ALIGN(8);
  } >SRAM

  /* 调试信息 */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  .debug_ranges   0 : { *(.debug_ranges) }

  /* 启动文件符号 */
  .ARM.attributes 0 : { KEEP (*(.ARM.attributes)) KEEP (*(.gnu.attributes)) }
  .comment       0 : { *(.comment) }
}

/* 检查栈空间 */
ASSERT((_estack - _ebss) >= 0x800, "Insufficient stack space")

/* RBL大小检查 - 不能超过32KB */
ASSERT((_edata - ORIGIN(SRAM)) <= 32768, "RBL size exceeds 32KB limit")
