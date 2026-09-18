/*
 * Copyright (c) 2017, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== CC1310_LAUNCHXL_NoRTOS.cmd ========
 */

--retain=g_pfnVectors
--retain=bootloaderApi

--stack_size=1024   /* C stack is also used for ISR stack */

--heap_size=256

/* Override default entry point.                                             */
--entry_point resetISR
/* Allow main() to take args                                                 */
--args 0x8
/* Suppress warnings and errors:                                             */
/* - 10063: Warning about entry point not being _c_int00                     */
/* - 16011, 16012: 8-byte alignment errors. Observed when linking in object  */
/*   files compiled using Keil (ARM compiler)                                */
--diag_suppress=10063,16011,16012

/* The starting address of the application.  Normally the interrupt vectors  */
/* must be located at the beginning of the application.                      */
#define FLASH_BASE              0x0
#define RAM_BASE                0x20000000
#define RAM_SIZE                0x5000

/* System memory map */

MEMORY
{
    /* Application stored in and executes from internal flash */
    BOOT0 (RX) : origin = 0x00000000, length = 0x00005000
    BOOT_API (RX) : origin = 0x00005000, length = 0x00000100
    BOOT1 (RX) : origin = 0x00005100, length = 0x00000F00
    CCFG (RX) : origin = 0x0001FFA8, length = 0x00000058
    /* Application uses internal RAM for data */
    SRAM (RWX) : origin = RAM_BASE, length = RAM_SIZE
}

/* Section allocation in memory */

SECTIONS
{
    .intvecs        :   > BOOT0
    .boot_api       :   > BOOT_API, palign(4)
    .text           :   >> BOOT0 | BOOT1
    .TI.ramfunc     : {} load=BOOT0 | BOOT1, run=SRAM, table(BINIT)
    .const          :   >> BOOT0 | BOOT1
    .constdata      :   >> BOOT0 | BOOT1
    .rodata         :   >> BOOT0 | BOOT1
    .cinit          :   > BOOT0 | BOOT1
    .pinit          :   > BOOT0 | BOOT1
    .init_array     :   > BOOT0 | BOOT1
    .emb_text       :   >> BOOT0 | BOOT1
    .args           :   > BOOT0 | BOOT1
    .ccfg           :   > CCFG

    .vtable_ram     :   > SRAM
    .data           :   > SRAM
    .bss            :   > SRAM
    .sysmem         :   > SRAM
    .nonretenvar    :   > SRAM

    .stack          :   > SRAM (HIGH)
}
