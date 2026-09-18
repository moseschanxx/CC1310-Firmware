--stack_size=1024
HEAPSIZE = 0x1800;
--entry_point ResetISR
--args 0x8
--diag_suppress=10063,16011,16012

MEMORY {
    FLASH (RX) : origin = 0x00008000, length = 0x00017000
    SRAM_APP    (RWX): origin = 0x20000000, length = 0x00004e00
    BOOT_DEBUG  (RW) : origin = 0x20004e00, length = 0x00000100
    BOOT_HANDOFF(RW) : origin = 0x20004f00, length = 0x00000100
}

SECTIONS {
    .text           : >> FLASH
    .TI.ramfunc     : {} load=FLASH, run=SRAM_APP, table(BINIT)
    .const          : >> FLASH
    .constdata      : >> FLASH
    .rodata         : >> FLASH
    .cinit          : > FLASH
    .pinit          : > FLASH
    .init_array     : > FLASH
    .emb_text       : >> FLASH
    .data           : > SRAM_APP
    .bss            : > SRAM_APP
    .sysmem         : > SRAM_APP
    .nonretenvar    : > SRAM_APP
    .priheap : { __primary_heap_start__ = .; . += HEAPSIZE; __primary_heap_end__ = .; } > SRAM_APP align 8
    .stack          : > SRAM_APP (HIGH)
    .boot_debug     : {} > BOOT_DEBUG
}
