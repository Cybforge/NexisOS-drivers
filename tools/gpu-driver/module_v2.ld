ENTRY(_start)
SECTIONS {
 . = 0;
 .text : { *(.text .text.*) }
 _text_end = .;
 . = ALIGN(4096);
 .rodata : { *(.rodata .rodata.*) }
 . = ALIGN(4096);
 _data_start = .;
 .data : { *(.data .data.*) }
 _image_end = .;
 .bss : { *(.bss .bss.*) *(COMMON) }
 . = ALIGN(4096);
 _memory_end = .;
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
