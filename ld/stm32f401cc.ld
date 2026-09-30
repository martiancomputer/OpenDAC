ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : {
    *(.text*) *(.rodata*)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .preinit_array : { KEEP(*(.preinit_array*)) } > FLASH
  .init_array : { KEEP(*(.init_array*)) } > FLASH
  .fini_array : { KEEP(*(.fini_array*)) } > FLASH
  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4); _sdata = .;
    *(.data*)
    . = ALIGN(4); _edata = .;
  } > RAM AT > FLASH
  .bss (NOLOAD) : {
    . = ALIGN(4); _sbss = .;
    *(.bss*) *(COMMON)
    . = ALIGN(4); _ebss = .;
  } > RAM
  . = ALIGN(8);
  _end = .;
  ASSERT(_end + 0x1000 <= ORIGIN(RAM) + LENGTH(RAM), "RAM stack guard")
}
