/* SPDX-License-Identifier: MIT */
/* MPS2-AN385 (Cortex-M3): flash at 0, SRAM at 0x20000000. .ARM.exidx is KEPT, without
   which the unwinder finds no tables and the arm cannot run. */
MEMORY { FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4M
         RAM   (rwx): ORIGIN = 0x20000000, LENGTH = 4M }
ENTRY(Reset_Handler)
SECTIONS {
  .text : { KEEP(*(.isr_vector)) *(.text*) *(.rodata*) } > FLASH
  .ARM.exidx : { __exidx_start = .; KEEP(*(.ARM.exidx*)) KEEP(*(.gnu.linkonce.armexidx.*)) __exidx_end = .; } > FLASH
  .ARM.extab : { KEEP(*(.ARM.extab*)) } > FLASH
  .tdata : { __tdata_start = .; *(.tdata*) __tdata_end = .; } > RAM AT > FLASH
  __tdata_load = LOADADDR(.tdata);
  .tbss (NOLOAD) : { *(.tbss*) *(.tcommon) } > RAM
  .data : { __data_start = .; *(.data*) __data_end = .; } > RAM AT > FLASH
  __data_load = LOADADDR(.data);
  .bss  : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; } > RAM
  . = ALIGN(8); __heap_start = .;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
