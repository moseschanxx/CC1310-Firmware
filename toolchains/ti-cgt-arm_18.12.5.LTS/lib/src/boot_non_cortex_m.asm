;******************************************************************************
;* BOOT                                                                       *
;*                                                                            *
;* Copyright (c) 1996 Texas Instruments Incorporated                          *
;* http://www.ti.com/                                                         *
;*                                                                            *
;*  Redistribution and  use in source  and binary forms, with  or without     *
;*  modification,  are permitted provided  that the  following conditions     *
;*  are met:                                                                  *
;*                                                                            *
;*     Redistributions  of source  code must  retain the  above copyright     *
;*     notice, this list of conditions and the following disclaimer.          *
;*                                                                            *
;*     Redistributions in binary form  must reproduce the above copyright     *
;*     notice, this  list of conditions  and the following  disclaimer in     *
;*     the  documentation  and/or   other  materials  provided  with  the     *
;*     distribution.                                                          *
;*                                                                            *
;*     Neither the  name of Texas Instruments Incorporated  nor the names     *
;*     of its  contributors may  be used to  endorse or  promote products     *
;*     derived  from   this  software  without   specific  prior  written     *
;*     permission.                                                            *
;*                                                                            *
;*  THIS SOFTWARE  IS PROVIDED BY THE COPYRIGHT  HOLDERS AND CONTRIBUTORS     *
;*  "AS IS"  AND ANY  EXPRESS OR IMPLIED  WARRANTIES, INCLUDING,  BUT NOT     *
;*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     *
;*  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT     *
;*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     *
;*  SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES  (INCLUDING, BUT  NOT     *
;*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     *
;*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     *
;*  THEORY OF  LIABILITY, WHETHER IN CONTRACT, STRICT  LIABILITY, OR TORT     *
;*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
;*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      *
;*                                                                            *
;******************************************************************************

;****************************************************************************
;* BOOT.ASM
;*
;* THIS IS THE INITAL BOOT ROUTINE FOR TMS470 C++ PROGRAMS.
;* IT MUST BE LINKED AND LOADED WITH ALL C++ PROGRAMS.
;*
;* THIS MODULE PERFORMS THE FOLLOWING ACTIONS:
;*   1) ALLOCATES THE STACK AND INITIALIZES THE STACK POINTER
;*   2) CALLS AUTO-INITIALIZATION ROUTINE
;*   3) CALLS THE FUNCTION MAIN TO START THE C++ PROGRAM
;*   4) CALLS THE STANDARD EXIT ROUTINE
;*
;* THIS MODULE DEFINES THE FOLLOWING GLOBAL SYMBOLS:
;*   1) __stack     STACK MEMORY AREA
;*   2) _c_int00    BOOT ROUTINE
;*
;****************************************************************************
	.armfunc _c_int00

   .if .TMS470_16BIS

;****************************************************************************
;*  16 BIT STATE BOOT ROUTINE                                               *
;****************************************************************************

	.arm

	.global	__stack
;***************************************************************
;* DEFINE THE USER MODE STACK (DEFAULT SIZE IS 512)
;***************************************************************
__stack:.usect	".stack", 0, 4

	.global	_c_int00

;***************************************************************
;* FUNCTION DEF: _c_int00
;***************************************************************
_c_int00: .asmfunc stack_usage(0)

	.if __TI_NEON_SUPPORT__ | __TI_VFP_SUPPORT__
        ;*------------------------------------------------------
	;* SETUP PRIVILEGED AND USER MODE ACCESS TO COPROCESSORS
	;* 10 AND 11, REQUIRED TO ENABLE NEON/VFP
	;* COPROCESSOR ACCESS CONTROL REG
	;* BITS [23:22] - CP11, [21:20] - CP10
	;* SET TO 0b11 TO ENABLE USER AND PRIV MODE ACCESS
        ;*------------------------------------------------------
	MRC      p15,#0x0,r0,c1,c0,#2
        MOV      r3,#0xf00000
	ORR      r0,r0,r3
        MCR      p15,#0x0,r0,c1,c0,#2

        ;*------------------------------------------------------
	; SET THE EN BIT, FPEXC[30] TO ENABLE NEON AND VFP
        ;*------------------------------------------------------
      	MOV      r0,#0x40000000
        FMXR     FPEXC,r0
        .endif ; __TI_NEON_SUPPORT__ | __TI_VFP_SUPPORT__

        ;------------------------------------------------------
	;* SET TO USER MODE
        ;*------------------------------------------------------
        MRS     r0, cpsr
        BIC     r0, r0, #0x1F  ; CLEAR MODES
        ORR     r0, r0, #0x10  ; SET USER MODE
        MSR     cpsr_cf, r0

        ;*------------------------------------------------------
	;* CHANGE TO 16 BIT STATE
        ;*------------------------------------------------------
	ADD	r0, pc, #1
	BX	r0

		.thumb

	;*------------------------------------------------------
        ;* INITIALIZE THE USER MODE STACK
        ;*------------------------------------------------------
	.if __TI_AVOID_EMBEDDED_CONSTANTS
	.thumb
	MOVW	r0, __STACK_END
	MOVT	r0, __STACK_END
	MOV	sp, r0
	.thumb
	.else ; __TI_AVOID_EMBEDDED_CONSTANTS
	LDR     r0, c_STACK_END
	MOV	sp, r0
	.endif ; __TI_AVOID_EMBEDDED_CONSTANTS


        ;*------------------------------------------------------
	;* Call the __mpu_init hook function.                   
        ;*------------------------------------------------------
        BL      __mpu_init

        ;*------------------------------------------------------
        ;* Perform all the required initializations when
        ;* _system_pre_init() returns non-zero:
        ;*   - Process BINIT Table
        ;*   - Perform C auto initialization
        ;*   - Call global constructors
        ;*------------------------------------------------------
        BL      _system_pre_init
        CMP     R0, #0
        BEQ     bypass_auto_init
        BL      __TI_auto_init
bypass_auto_init:

        ;*------------------------------------------------------
	;* CALL APPLICATION
        ;*------------------------------------------------------
        BL      _args_main

        ;*------------------------------------------------------
	;* IF APPLICATION DIDN'T CALL EXIT, CALL EXIT(1)
        ;*------------------------------------------------------
        MOVS    r0, #1
        BL      exit

        ;*------------------------------------------------------
	;* DONE, LOOP FOREVER
        ;*------------------------------------------------------
L1:     B	L1
	.endasmfunc

   .else           ; !.TMS470_16BIS

;****************************************************************************
;*  32 BIT STATE BOOT ROUTINE                                               *
;****************************************************************************

        .global __stack
;***************************************************************
;* DEFINE THE USER MODE STACK (DEFAULT SIZE IS 512)
;***************************************************************
__stack:.usect  ".stack", 0, 4

        .global _c_int00
;***************************************************************
;* FUNCTION DEF: _c_int00
;***************************************************************
_c_int00: .asmfunc stack_usage(0)

        .if __TI_NEON_SUPPORT__ | __TI_VFP_SUPPORT__
        ;*------------------------------------------------------
        ;* SETUP PRIVILEGED AND USER MODE ACCESS TO COPROCESSORS
        ;* 10 AND 11, REQUIRED TO ENABLE NEON/VFP
        ;* COPROCESSOR ACCESS CONTROL REG
        ;* BITS [23:22] - CP11, [21:20] - CP10
        ;* SET TO 0b11 TO ENABLE USER AND PRIV MODE ACCESS
        ;*------------------------------------------------------
        MRC      p15,#0x0,r0,c1,c0,#2
        MOV      r3,#0xf00000
        ORR      r0,r0,r3
        MCR      p15,#0x0,r0,c1,c0,#2

        ;*------------------------------------------------------
        ; SET THE EN BIT, FPEXC[30] TO ENABLE NEON AND VFP
        ;*------------------------------------------------------
        MOV      r0,#0x40000000
        FMXR     FPEXC,r0
        .endif

        ;*------------------------------------------------------
        ;* SET TO USER MODE
        ;*------------------------------------------------------
        MRS     r0, cpsr
        BIC     r0, r0, #0x1F  ; CLEAR MODES
        ORR     r0, r0, #0x10  ; SET USER MODE
        MSR     cpsr_cf, r0

        ;*------------------------------------------------------
        ;* INITIALIZE THE USER MODE STACK
        ;*------------------------------------------------------
        .if __TI_AVOID_EMBEDDED_CONSTANTS
        MOVW    sp, __STACK_END
        MOVT    sp, __STACK_END
        .else
        LDR     sp, c_STACK_END
        .endif

        ;*------------------------------------------------------
	;* Call the __mpu_init hook function.
        ;*------------------------------------------------------
        BL      __mpu_init

        ;*------------------------------------------------------
        ;* Perform all the required initializations when
        ;* _system_pre_init() returns non-zero:
        ;*   - Process BINIT Table
        ;*   - Perform C auto initialization
        ;*   - Call global constructors
        ;*------------------------------------------------------
        BL      _system_pre_init
        CMP     R0, #0
        BEQ     bypass_auto_init
        BL      __TI_auto_init
bypass_auto_init:

        ;*------------------------------------------------------
        ;* CALL APPLICATION
        ;*------------------------------------------------------
        BL      _args_main

        ;*------------------------------------------------------
        ;* IF APPLICATION DIDN'T CALL EXIT, CALL EXIT(1)
        ;*------------------------------------------------------
        MOV     R0, #1
        BL      exit

        ;*------------------------------------------------------
        ;* DONE, LOOP FOREVER
        ;*------------------------------------------------------
L1:     B       L1
        .endasmfunc

   .endif    ; !.TMS470_16BIS

;***************************************************************
;* CONSTANTS USED BY THIS MODULE
;***************************************************************
        .if !__TI_AVOID_EMBEDDED_CONSTANTS
c_STACK_END    .long    __STACK_END
        .endif

;******************************************************
;* UNDEFINED REFERENCES                               *
;******************************************************
	.global	__STACK_END
        .global _system_pre_init
        .global __TI_auto_init
	.global _args_main
	.global	exit
	.global	__mpu_init

	.end
