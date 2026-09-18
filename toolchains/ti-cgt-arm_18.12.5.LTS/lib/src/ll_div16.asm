;******************************************************************************
;* LL_DIV16.ASM  - 16 BIT STATE -                                             *
;*                                                                            *
;* Copyright (c) 1995 Texas Instruments Incorporated                          *
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

;******************************************************************************
;* __aeabi_ldivmod - Divide two signed long long integers
;******************************************************************************

        .thumb

        .global __aeabi_ldiv0
        .global __aeabi_uldivmod

        .if .TMS470_LITTLE

dvnd_hi .set    r1  ; High word of dividend
dvnd_lo .set    r0  ; Low word of dividend
idvs_hi .set    r3  ; High word of divisor
idvs_lo .set    r2  ; Low word of divisor

rq_hi   .set    r1  ; High word of result quo
rq_lo   .set    r0  ; Low word of result quo
rr_hi   .set    r3  ; High word of result rem
rr_lo   .set    r2  ; Low word of result rem

        .else

dvnd_hi .set    r0  ; High word of dividend
dvnd_lo .set    r1  ; Low word of dividend
idvs_hi .set    r2  ; High word of divisor
idvs_lo .set    r3  ; Low word of divisor

rq_hi   .set    r0  ; High word of result quo
rq_lo   .set    r1  ; Low word of result quo
rr_hi   .set    r2  ; High word of result rem
rr_lo   .set    r3  ; Low word of result rem

        .endif

sign    .set    r4
tmp1    .set    r5


        .thumbfunc __aeabi_ldivmod
        .global __aeabi_ldivmod

__aeabi_ldivmod:        .asmfunc stack_usage(12)

   PUSH   {r4-r5, lr}

; Check if the divisor is 0
   CMP idvs_hi, #0
   BNE $nonzero_dvsr
   CMP idvs_lo, #0
   BNE $nonzero_dvsr

; ARM EABI Run-time ABI says when dividing by zero, this function must call
; __aeabi_ldiv0 with an argument depending on the value of the dividend,
; LLONG_MAX if the dividend is greater than zero, LLONG_MIN if the dividend is
; less than zero, otherwise 0. The remainder should be either 0 or the original
; dividend.

   MOVS   rr_hi, dvnd_hi
   MOVS   rr_lo, dvnd_lo

   CMP    dvnd_hi, #0
   BNE    $100
   CMP    dvnd_lo, #0
   BNE    $100
   MOVS   rq_lo, #0
   MOVS   rq_hi, #0
   B      call_ldiv0

$100:
   LSRS   dvnd_hi, dvnd_hi, #32
   BCS    $101
; Positive case, LLONG_MAX
   MOVS  rq_lo, #1
   NEGS  rq_hi, rq_lo
   LSRS  rq_hi, rq_hi, #1
   NEGS  rq_lo, rq_lo
   B      call_ldiv0

; Negative case, LLONG_MIN
$101:
   MOVS   rq_lo, #0
   MOVS   rq_hi, #1
   LSLS   rq_hi, rq_hi, #31

; Call to __aeabi_ldiv0
call_ldiv0:
   BL    __aeabi_ldiv0
   POP   {r4-r5,pc}

$nonzero_dvsr:
; Store the sign of remainder which is the sign of the dividend in bit 31 of
; sign. Store the sign of the quotient which is sign of divisor xored with sign
; of dividend in bit 30 of sign.

   LSRS   sign, idvs_hi, #1
   ASRS   tmp1, dvnd_hi, #1
   EORS   sign, sign, tmp1
   BPL    $1
   MOVS   tmp1, #0
; Take the absolute value of dvnd
   CMP    dvnd_hi, #0
   BGE    $1
   MOVS   tmp1, dvnd_hi
   MOVS   dvnd_hi, #0
   NEGS   dvnd_lo, dvnd_lo
   SBCS   dvnd_hi, tmp1

$1:
   MOVS   tmp1, #1
   LSLS   tmp1, tmp1, #31
   TST    idvs_hi, tmp1
   BEQ    $2
   MOVS   tmp1, #0
; Take the absolute value of dvsr
   CMP    idvs_hi, #0
   BGE    $2
   MOVS   tmp1, idvs_hi
   MOVS   idvs_hi, #0
   NEGS   idvs_lo, idvs_lo
   SBCS   idvs_hi, tmp1
$2:

; Use unsigned division for the actual calculations
   BL     __aeabi_uldivmod

; Set the correct sign on the quotient and remainder
   MOVS   sign, sign
   BPL    $3

   MOVS   tmp1, #0
   RSBS   rr_lo, rr_lo, #0
   SBCS   tmp1, rr_hi
   MOVS   rr_hi, tmp1

$3:
   LSLS   sign, sign, #1
   BPL    $4
   MOVS   tmp1, #0
   RSBS   rq_lo, rq_lo, #0
   SBCS   tmp1, rq_hi
   MOVS   rq_hi, tmp1

$4:
   POP    {r4-r5, pc}
        .endasmfunc
