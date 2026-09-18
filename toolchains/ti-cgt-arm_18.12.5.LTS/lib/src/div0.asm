;******************************************************************************
;* DIV0.ASM                                                                   *
;* Copyright (c) 2017@%%%% Texas Instruments Incorporated                     *
;******************************************************************************

; ARM EABI describes the functions __aeabi_idiv0 and __aeabi_ldiv0 which are
; called when a 32-bit or 64-bit divide by 0 occurs. Each function accepts and
; returns the quotient and remainder. The functions can be overridden to
; implement system specific implementations like returning a fixed value or
; raising an exception

        .weak __aeabi_idiv0
        .weak __aeabi_ldiv0

__aeabi_idiv0   .set internal_div0
__aeabi_ldiv0   .set internal_div0

internal_div0:
   BX lr
