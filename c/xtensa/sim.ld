/* Memory for QEMU's Xtensa sim machine with the lx106 core: 64 MB at
   0x60000000, and the reset vector at 0x50000000. Literal pools go before
   the code that loads them, as Xtensa needs. */
ENTRY(_start)
MEMORY {
  rom : ORIGIN = 0x50000000, LENGTH = 4K
  ram : ORIGIN = 0x60000000, LENGTH = 64M
}
SECTIONS {
  .reset : { KEEP(*(.reset)) } > rom
  .text : {
    *(.literal .text .literal.* .text.* .stub .gnu.linkonce.literal.* .gnu.linkonce.t.*)
    *(.init .init.literal .fini .fini.literal)
  } > ram
  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.* .srodata*) . = ALIGN(8); } > ram
  .data : { *(.data .data.* .sdata* .gnu.linkonce.d.*) . = ALIGN(8); } > ram
  .tdata : { PROVIDE(__tls_base = .); *(.tdata .tdata.*) } > ram
  .tbss : { *(.tbss .tbss.*) . = ALIGN(8); } > ram
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* .sbss* COMMON) . = ALIGN(16); __bss_end = .; } > ram
  __heap_start = .;
  __stack_top = ORIGIN(ram) + LENGTH(ram);
  __heap_end = __stack_top - 0x100000;
}
