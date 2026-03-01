ENTRY(_start)

SECTIONS
{
  /* Kernel ładowany przez OpenSBI */
  . = 0x80200000;

  _kernel_start = .;

  /* ---------------- TEXT ---------------- */
  .text : ALIGN(0x1000)
  {
    _text_start = .;
    *(.text .text.*)
    _text_end = .;
  }

  /* ---------------- RODATA ---------------- */
  .rodata : ALIGN(0x1000)
  {
    _rodata_start = .;
    *(.rodata .rodata.*)
    _rodata_end = .;
  }

  /* ---------------- DATA ---------------- */
  .data : ALIGN(0x1000)
  {
    _data_start = .;
    *(.data .data.*)
    _data_end = .;
  }

  /* ---------------- BSS ---------------- */
  .bss : ALIGN(0x1000)
  {
    _bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    _bss_end = .;
  }

  /* Wyrównanie do strony */
  . = ALIGN(0x1000);
  _kernel_end = .;

  /* ---------------- STACK ---------------- */
  .stack (NOLOAD) : ALIGN(16)
  {
    _stack_bottom = .;
    . += 0x4000;   /* 16 KB */
    _stack_top = .;
  }

  _kernel_image_end = .;
}