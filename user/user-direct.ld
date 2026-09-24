/* lab2 direct-map link script. The course gift user.ld remains unchanged. */
OUTPUT_ARCH(riscv)
ENTRY(main)

SECTIONS
{
  . = 0x82000000;

  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : {
    *(.bss .bss.*)
    *(.sbss*)
  }

  end = .;
}
