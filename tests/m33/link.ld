/* CLATTER 12 instruction-count bench: QEMU mps2-an505 (Cortex-M33), everything in SSRAM1. */
MEMORY { RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 4M }
ENTRY(reset_handler)
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.time_critical*) *(.rodata*) . = ALIGN(4); } > RAM
  .ARM.exidx : { *(.ARM.exidx*) } > RAM
  .data : { *(.data*) . = ALIGN(4); } > RAM
  .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end__ = .; } > RAM
  . = ALIGN(8);
  end = .; _end = .; __end__ = .;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
