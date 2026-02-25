ENTRY(_start)

SECTIONS
{
  . = 0x80200000;

  .text : {
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
  }

  . = ALIGN(0x1000);
  stack_bottom = .;
  . += 0x4000;
  stack_top = .;
}
