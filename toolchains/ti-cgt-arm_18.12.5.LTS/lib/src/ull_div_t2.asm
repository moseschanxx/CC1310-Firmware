;******************************************************************************
;* ULL_DIV_T2.ASM  - THUMB-2 STATE -                                          *
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

;******************************************************************************
;* __aeabi_uldivmod - Divide two unsigned long long integers
;******************************************************************************

        .global __aeabi_ldiv0

        .thumb

        .if .TMS470_LITTLE

idvnd_hi .set    r1     ; High word of dividend
idvnd_lo .set    r0     ; Low word of dividend
idvsr_hi .set    r3     ; High word of divisor
idvsr_lo .set    r2     ; Low word of divisor
rq_hi    .set    r1     ; High word of result quo
rq_lo    .set    r0     ; Low word of result quo
rr_hi    .set    r3     ; High word of result rem
rr_lo    .set    r2     ; High word of result rem

        .else

idvnd_hi .set    r0     ; High word of dividend
idvnd_lo .set    r1     ; low word of dividend
idvsr_hi .set    r2     ; High word of divisor
idvsr_lo .set    r3     ; Low word of divisor
rq_hi    .set    r0     ; High word of result quo
rq_lo    .set    r1     ; Low word of result quo
rr_hi    .set    r2     ; High word of result rem
rr_lo    .set    r3     ; High word of result rem

        .endif

; The divisor is passed in r2:r3 which is used to return the remainder. These
; registers are used to make a copy.
dvsr_hi .set r4
dvsr_lo .set r5

; The remainder will be computed by adjusting the dvnd registers so use the
; remainder return register to store the dividend
dvnd_hi .set rr_hi
dvnd_lo .set rr_lo

; Temporary registers used during the calculation
tmp_hi  .set    r6      ; HIGH WORD OF TEMP
tmp_lo  .set    r7      ; LOW WORD OF TEMP


        .thumbfunc __aeabi_uldivmod
        .global __aeabi_uldivmod

__aeabi_uldivmod: .asmfunc stack_usage(20)

; The prolog of the function performs verification on the inputs. In order to
; perform a tail call to __aeabi_ldiv0 the frame isn't allocated until we
; know the divisor is non-zero (see not_zero label). Be careful not to use
; any regiseters except r0-r3 until the not_zero label is reached.

; If the dividend is less than the divisor, Q=0 and R=dividend
  CMP    idvsr_hi, idvnd_hi
  IT     EQ
  CMPEQ  idvsr_lo, idvnd_lo
  BLS    greater_or_equal

  MOVS   rr_hi, idvnd_hi
  MOVS   rr_lo, idvnd_lo
  MOVS   rq_hi, #0
  MOVS   rq_lo, #0
  BX     lr

greater_or_equal:

; Check if the divisor is 0

  CMP    idvsr_hi, #0
  IT	 EQ
  CMPEQ  idvsr_lo, #0
  BNE    not_zero

; ARM EABI Run-time ABI says when dividing by zero, this function must call
; __aeabi_ldiv0 with an argument depending on the value of the dividend,
; ULLONG_MAX If the dividend is greater than zero otherwise 0. The remainder
; should be either 0 or the original dividend.

  MOVS   rr_hi, idvnd_hi
  MOVS   rr_lo, idvnd_lo
  MOVS   rq_hi, #0
  MOVS   rq_lo, #0

  CMP    rr_hi, #0
  IT     EQ
  CMPEQ  rr_lo, #0

  ITT    NE
  MVNNE  rq_hi, rq_hi
  MVNNE  rq_lo, rq_lo

; Tail call to __aeabi_ldiv0.
  B      __aeabi_ldiv0

; We now know that the divisor is non-zero and dividend >= divisor. Allocate
; the frame and proceed.
not_zero:
  PUSH   {r4-r7, lr}

; Parameter setup. Copy the inputs to their correct locations for the algorithm.
  MOV    dvsr_hi, idvsr_hi
  MOV    dvsr_lo, idvsr_lo

  MOV    dvnd_hi, idvnd_hi
  MOV    dvnd_lo, idvnd_lo

; Initialize the quotient to 0
  MOVS   rq_lo, #0
  MOVS   rq_hi, #0

; Compute the amount that we need to left shift the divisor. The shift amount is
; CLZ(dvsr)-CLZ(dvnd)

  CLZ    tmp_hi, dvsr_hi
  CMP    tmp_hi, #32
  ITT    EQ
  CLZEQ  tmp_hi, dvsr_lo
  ADDEQ  tmp_hi, #32

  CLZ    tmp_lo, dvnd_hi
  CMP    tmp_lo, #32
  ITT    EQ
  CLZEQ  tmp_lo, dvnd_lo
  ADDEQ  tmp_lo, #32

  SUBS   tmp_hi, tmp_hi, tmp_lo

; Now left shift the divisor by the shift amount
  SUBS   tmp_lo, tmp_hi, #32
  ITTEE  CS
  LSLCS  dvsr_hi, dvsr_lo, tmp_lo
  MOVCS  dvsr_lo, #0
  RSBCC  tmp_lo, tmp_lo, #0
  LSLCC  dvsr_hi, dvsr_hi, tmp_hi
  ITTT   CC
  LSRCC  tmp_lo, dvsr_lo, tmp_lo
  ORRCC  dvsr_hi, dvsr_hi, tmp_lo
  LSLCC  dvsr_lo, dvsr_lo, tmp_hi


; For shift+1 iterations, if dividend is larger than divisor, shift a 1 into the
; quotient and subtract the divisor, else shift a 0 into the quotient.
divll:
  CMP    dvnd_hi, dvsr_hi
  IT	 EQ
  CMPEQ  dvnd_lo, dvsr_lo
  BCC    $4
  SUBS   dvnd_lo, dvnd_lo, dvsr_lo
  SBCS   dvnd_hi, dvnd_hi, dvsr_hi
$4:
  ADCS   rq_lo, rq_lo, rq_lo
  ADCS   rq_hi, rq_hi, rq_hi

; Right shift the divisor by 1
  LSRS   dvsr_lo, dvsr_lo, #1
  ORR    dvsr_lo, dvsr_lo, dvsr_hi, LSL #31
  LSRS   dvsr_hi, dvsr_hi, #1

  SUBS   tmp_hi, tmp_hi, #1
  BPL    divll

  POP    {r4-r7, pc}
     .endasmfunc
