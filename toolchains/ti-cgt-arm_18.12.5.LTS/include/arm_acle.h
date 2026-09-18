/*****************************************************************************/
/*  arm_acle.h                                                               */
/*                                                                           */
/* Copyright (c) 2017 Texas Instruments Incorporated                         */
/* http://www.ti.com/                                                        */
/*                                                                           */
/*  Redistribution and  use in source  and binary forms, with  or without    */
/*  modification,  are permitted provided  that the  following conditions    */
/*  are met:                                                                 */
/*                                                                           */
/*     Redistributions  of source  code must  retain the  above copyright    */
/*     notice, this list of conditions and the following disclaimer.         */
/*                                                                           */
/*     Redistributions in binary form  must reproduce the above copyright    */
/*     notice, this  list of conditions  and the following  disclaimer in    */
/*     the  documentation  and/or   other  materials  provided  with  the    */
/*     distribution.                                                         */
/*                                                                           */
/*     Neither the  name of Texas Instruments Incorporated  nor the names    */
/*     of its  contributors may  be used to  endorse or  promote products    */
/*     derived  from   this  software  without   specific  prior  written    */
/*     permission.                                                           */
/*                                                                           */
/*  THIS SOFTWARE  IS PROVIDED BY THE COPYRIGHT  HOLDERS AND CONTRIBUTORS    */
/*  "AS IS"  AND ANY  EXPRESS OR IMPLIED  WARRANTIES, INCLUDING,  BUT NOT    */
/*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR    */
/*  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT    */
/*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    */
/*  SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES  (INCLUDING, BUT  NOT    */
/*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,    */
/*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY    */
/*  THEORY OF  LIABILITY, WHETHER IN CONTRACT, STRICT  LIABILITY, OR TORT    */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE    */
/*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.     */
/*                                                                           */
/*                                                                           */
/*  TI Implementation of ARM C Language Extensions                           */
/*  per document: "ARM C Language Extensions Release 2.0"                    */
/*  ARM doc num:  IHI 0053C                                                  */
/*  Issue date:   09/05/2014                                                 */
/*                                                                           */
/*****************************************************************************/
#ifndef _TI_ARM_ACLE_H
#define _TI_ARM_ACLE_H

#ifndef __ARM_ACLE
#error "ARM C Language Extensions (ACLE) not enabled"
#endif

#include <stdint.h>
#include <_ti_config.h>

#define __CONST(lb, ub)  __attribute__((constrange((lb), (ub))))
#define __BUILTIN        __attribute__((builtin))
#define __IMPURE         __attribute__((impure))
#define __BUILTIN_M      __BUILTIN __IMPURE

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Synchronization, Barrier, and Hint Intrinsics (Chapter 8)                 */
/*===========================================================================*/
/*---------------------------------------------------------------------------*/
/* Memory Barriers (Section 8.3)                                             */
/* NOTE: The ACLE spec describes an integer constant argument to these       */
/*       intrinsics to indicate the semantics associated with the barrier    */
/*       (see table in section 8.3). The current TI ARM Compiler does not    */
/*       support an integer constant argument to these intrinsics.           */
/*---------------------------------------------------------------------------*/
/*****************************************************************************/
/* Data Memory Barrier                                                       */
/*****************************************************************************/
void __BUILTIN_M _dmb(void);
/* The TI ARM Compiler does not currently support an argument to the _dmb
** intrinsic. The _dmb() intrinsic above will encode a 0xf in the "option"
** field (bits 0-3) of the DMB instruction's opcode.
** void __BUILTIN_M __dmb(unsigned int k);
*/

/*****************************************************************************/
/* Data Synchronization Barrier                                              */
/*****************************************************************************/
void __BUILTIN_M _dsb(void);
/* The TI ARM Compiler does not currently support an argument to the _dsb
** intrinsic. The _dsb() intrinsic above will encode a 0xf in the "option"
** field (bits 0-3) of the DSB instruction's opcode.
** void __BUILTIN_M __dsb(unsigned int k);
*/

/*****************************************************************************/
/* Instruction Synchronization Barrier                                       */
/*****************************************************************************/
void __BUILTIN_M _isb(void);
/* The TI ARM Compiler does not currently support an argument to the _isb
** intrinsic. The _isb() intrinsic above will encode a 0xf in the "option"
** field (bits 0-3) of the ISB instruction's opcode.
** void __BUILTIN_M __isb(unsigned int k);
*/

/*---------------------------------------------------------------------------*/
/* Hints (Section 8.4)                                                       */
/*---------------------------------------------------------------------------*/
/*****************************************************************************/
/* Wait for Interrupt Hint                                                   */
/*****************************************************************************/
void __BUILTIN_M __wfi(void);

/*****************************************************************************/
/* Wait for Event Hint                                                       */
/*****************************************************************************/
void __BUILTIN_M __wfe(void);

/*****************************************************************************/
/* Send a Global Event Hint                                                  */
/*****************************************************************************/
/* SEV intrinsic is not supported in TI ARM Compiler
** void __sev(void);
*/

/*****************************************************************************/
/* Send a Local Event Hint                                                   */
/*****************************************************************************/
/* Local SEV intrinsic is not supported in TI ARM Compiler
** void __sevl(void);
*/

/*****************************************************************************/
/* Yield Hint                                                                */
/*****************************************************************************/
/* YIELD intrinsic is not supported in TI ARM Compiler
** void __yield(void);
*/

/*****************************************************************************/
/* Debug Hint                                                                */
/*****************************************************************************/
/* DBG intrinsic is not supported in TI ARM Compiler
** void __dbg(void);
*/

/*---------------------------------------------------------------------------*/
/* Swap (Section 8.5)                                                        */
/*---------------------------------------------------------------------------*/
_IDEFN uint32_t __attribute__((__always_inline__)) 
__swp(uint32_t x, uint32_t *p)
{
   uint32_t v;
   do v = __ldrex(p); while (__strex(x, p));
   return v;
}

/*---------------------------------------------------------------------------*/
/* Memory Prefetch Intrinsics (Section 8.6)                                  */
/*---------------------------------------------------------------------------*/
/*****************************************************************************/
/* Data Prefetch                                                             */
/*****************************************************************************/
/* Prefetch intrinsics are not supported in TI ARM Compiler
** void __BUILTIN_M
** __pldx(__CONST(0, 1) unsigned int access_kind,
**        __CONST(0, 2) unsigned int cache_level, 
**        __CONST(0, 1) unsigned int retention_policy,
**        void const volatile       *addr);
** #define __pld(addr)	__pldx(0,0,0,addr)
*/

/*****************************************************************************/
/* Instruction Prefetch                                                      */
/*****************************************************************************/
/* Prefetch intrinsics are not supported in TI ARM Compiler
** void __BUILTIN_M
** __plix(__CONST(0, 2) unsigned int cache_level, 
**        __CONST(0, 1) unsigned int retention_policy,
**        void const volatile       *addr);
** #define __pli(addr)	__plix(0,0,addr)
*/

/*---------------------------------------------------------------------------*/
/* NOP (Section 8.7)                                                         */
/*---------------------------------------------------------------------------*/
void __BUILTIN_M __nop(void);

/*===========================================================================*/
/* Data Processing Intrinsics (Chapter 9)                                    */
/*===========================================================================*/
/*---------------------------------------------------------------------------*/
/* Q Saturation Flag (Section 9.1.1)                                         */
/*---------------------------------------------------------------------------*/
/* Q-Bit manipulation intrinsics are not supported in TI ARM Compiler
** int  __BUILTIN_M __saturation_occurred(void);
** void __BUILTIN_M __set_saturation_occurred(int n);
** void __BUILTIN_M __ignore_saturation(void);
*/

/*---------------------------------------------------------------------------*/
/* Miscellaneous Data-Processing Intrinsics (Section 9.2)                    */
/*---------------------------------------------------------------------------*/
uint32_t      __BUILTIN __ror  (uint32_t x, uint32_t y);
#define __rorl		__ror
/* TI ARM Compiler does not support uint64_t form of __ror intrinsic
** uint64_t      __BUILTIN __rorll(uint64_t x, uint64_t y);
*/

#if defined(__ARM_FEATURE_CLZ)
unsigned int  __BUILTIN __clz  (uint32_t x);
#define __clzl		__clz
/* TI ARM Compiler does not support uint64_t form of __clz intrinsic
** unsigned int __BUILTIN __clzll(uint64_t x);
*/
#endif

/* CLS instruction is not available in Cortex-M or Cortex-R architectures
** unsigned int  __BUILTIN __cls  (uint32_t x);
** unsigned int  __BUILTIN __clsl (unsigned long x);
** unsigned int  __BUILTIN __clsll(uint64_t x);
*/

uint32_t      __BUILTIN __rev  (uint32_t x);
#define __revl		__rev
/* TI ARM Compiler does not support uint64_t form of __rev intrinsic
** uint64_t      __BUILTIN __revll(uint64_t x);
*/

uint32_t      __BUILTIN __rev16  (uint32_t x);
#define __rev16l	__rev16
/* TI ARM Compiler does not support uint64_t form of __rev16 intrinsic
** uint64_t      __BUILTIN __rev16ll(uint64_t x);
*/

/*---------------------------------------------------------------------------*/
/* The ACLE spec says the argument and return type should be int16_t, but    */
/* the REVSH instruction will reverse the byte-order of the lower half of    */
/* the 32-bit source register, then sign extend the result. So we assert     */
/* that the argument and return types for the __revsh() intrinsic should be  */
/* int32_t.                                                                  */
/*---------------------------------------------------------------------------*/
int16_t       __BUILTIN __revsh (int16_t x);

uint32_t      __BUILTIN __rbit  (uint32_t x);
#define __rbitl		__rbit
/* TI ARM Compiler does not support uint64_t form of __rbit intrinsic
** uint64_t      __BUILTIN __rbitll(uint64_t x);
*/

/*---------------------------------------------------------------------------*/
/* 16-Bit Multiplications (Section 9.3)                                      */
/*---------------------------------------------------------------------------*/
#if defined(__ARM_FEATURE_DSP)
int32_t __BUILTIN _smulbb(int32_t x, int32_t y);
#define __smulbb	_smulbb

int32_t __BUILTIN _smulbt(int32_t x, int32_t y);
#define __smulbt	_smulbt

int32_t __BUILTIN _smultb(int32_t x, int32_t y);
#define __smultb	_smultb

int32_t __BUILTIN _smultt(int32_t x, int32_t y);
#define __smultt	_smultt

int32_t __BUILTIN _smulwb(int32_t x, int32_t y);
#define __smulwb	_smulwb

int32_t __BUILTIN _smulwt(int32_t x, int32_t y);
#define __smulwt	_smulwt
#endif

/*---------------------------------------------------------------------------*/
/* Saturating Intrinsics (Section 9.4)                                       */
/*---------------------------------------------------------------------------*/
/*****************************************************************************/
/* Width-Specified Saturation Intrinsics (Section 9.4.1)                     */
/*****************************************************************************/
int32_t  __BUILTIN 
_ssata(int32_t                    x, 
       __CONST(0,31) unsigned int shift, 
       __CONST(1,32) unsigned int y);
#define __ssat(x, y)	_ssata(x, 0, y)

uint32_t __BUILTIN 
_usata(int32_t                    x, 
       __CONST(0,31) unsigned int shift, 
       __CONST(0,31) unsigned int y);
#define __usat(x, y)    _usata(x, 0, y)

#if defined(__ARM_FEATURE_DSP)
/*****************************************************************************/
/* Saturating Addition and Subtraction Intrinsics (Section 9.4.2)            */
/*****************************************************************************/
int32_t __BUILTIN _sadd(int32_t x, int32_t y);
#define __qadd		_sadd

int32_t __BUILTIN _ssub(int32_t x, int32_t y);
#define __qsub		_ssub

/* QDBL intrinsic is not supported in TI ARM Compiler
** int32_t __BUILTIN __qdbl(int32_t x);
*/

/*****************************************************************************/
/* Accumulating Multiplications (Section 9.4.3)                              */
/*****************************************************************************/
int32_t __BUILTIN _smlabb(int32_t x, int32_t y, int32_t z);
#define __smlabb	_smlabb

int32_t __BUILTIN _smlabt(int32_t x, int32_t y, int32_t z);
#define __smlabt	_smlabt

int32_t __BUILTIN _smlatb(int32_t x, int32_t y, int32_t z);
#define __smlatb	_smlatb

int32_t __BUILTIN _smlatt(int32_t x, int32_t y, int32_t z);
#define __smlatt	_smlatt

int32_t __BUILTIN _smlawb(int32_t x, int32_t y, int32_t z);
#define __smlawb	_smlawb

int32_t __BUILTIN _smlawt(int32_t x, int32_t y, int32_t z);
#define __smlawt	_smlawt

/*---------------------------------------------------------------------------*/
/* 32-Bit SIMD Intrinsics (Section 9.5)                                      */
/*---------------------------------------------------------------------------*/
/*****************************************************************************/
/* Data Types for 32-Bit SIMD Intrinsics (Section 9.5.2)                     */
/*****************************************************************************/
typedef int32_t  int8x4_t;
typedef uint32_t uint8x4_t;
typedef int32_t  int16x2_t;
typedef uint32_t uint16x2_t;

/*****************************************************************************/
/* Parallel 16-Bit Saturation (Section 9.5.4)                                */
/*****************************************************************************/
int16x2_t __BUILTIN _ssat16(int16x2_t x, __CONST(1,16) unsigned int y);
#define __ssat16	_ssat16

int16x2_t __BUILTIN _usat16(int16x2_t x, __CONST(0,15) unsigned int y);
#define __usat16	_usat16

/*****************************************************************************/
/* Packing and Unpacking (Section 9.5.5)                                     */
/* Note: Different arguments for ACLE version v. TI ARM compiler version     */
/*****************************************************************************/
int16x2_t  __BUILTIN _sxtab16(int16x2_t x, int8x4_t y, int32_t z);
#define __sxtab16(x, y)	_sxtab16(x, y, 0)

int16x2_t  __BUILTIN _sxtb16(int16x2_t x1, int32_t x2);
#define __sxtb16(x)	_sxtb16(x, 0)

uint16x2_t __BUILTIN _uxtab16(uint16x2_t x, uint8x4_t y, int32_t z);
#define __uxtab16(x, y)	_uxtab16(x, y, 0)

uint16x2_t __BUILTIN _uxtb16(uint16x2_t x1, int32_t x2);
#define __uxtb16(x)	_uxtb16(x, 0)

/*****************************************************************************/
/* Parallel Selection (Section 9.5.6)                                        */
/* GE Flags (Section 9.1.2) - can use __sel() to read or use GE bits         */
/*****************************************************************************/
uint8x4_t __BUILTIN _sel(uint8x4_t x, uint8x4_t y);
#define __sel		_sel

/*****************************************************************************/
/* Parallel 8-Bit Addition and Subtraction (Section 9.5.7)                   */
/*****************************************************************************/
int8x4_t  __BUILTIN _qadd8(int8x4_t x, int8x4_t y);
#define __qadd8		_qadd8

int8x4_t  __BUILTIN _qsub8(int8x4_t x, int8x4_t y);
#define __qsub8		_qsub8

int8x4_t  __BUILTIN _sadd8(int8x4_t x, int8x4_t y);
#define __sadd8		_sadd8

int8x4_t  __BUILTIN _shadd8(int8x4_t x, int8x4_t y);
#define __shadd8	_shadd8

int8x4_t  __BUILTIN _shsub8(int8x4_t x, int8x4_t y);
#define __shsub8	_shsub8

int8x4_t  __BUILTIN _ssub8(int8x4_t x, int8x4_t y);
#define __ssub8		_ssub8

uint8x4_t __BUILTIN _uadd8(uint8x4_t x, uint8x4_t y);
#define __uadd8		_uadd8

uint8x4_t __BUILTIN _uhadd8(uint8x4_t x, uint8x4_t y);
#define __uhadd8	_uhadd8

uint8x4_t __BUILTIN _uhsub8(uint8x4_t x, uint8x4_t y);
#define __uhsub8	_uhsub8

uint8x4_t __BUILTIN _uqadd8(uint8x4_t x, uint8x4_t y);
#define __uqadd8	_uqadd8

uint8x4_t __BUILTIN _uqsub8(uint8x4_t x, uint8x4_t y);
#define __uqsub8	_uqsub8

uint8x4_t __BUILTIN _usub8(uint8x4_t x, uint8x4_t y);
#define __usub8		_usub8

/*****************************************************************************/
/* Sum of 8-Bit Absolute Differences (Section 9.5.8)                         */
/* Note: different argument order on __usada8 v. _usada8                     */
/*****************************************************************************/
uint32_t __BUILTIN _usad8(uint8x4_t x, uint8x4_t y);
#define __usad8			_usad8

uint32_t __BUILTIN _usada8(uint32_t z, uint8x4_t x, uint8x4_t y);
#define __usada8(x, y, z)	_usada8(z, x, y)

/*****************************************************************************/
/* Parallel 16-Bit Addition and Subtraction (Section 9.5.9)                  */
/*****************************************************************************/
int16x2_t  __BUILTIN _qadd16(int16x2_t x, int16x2_t y);
#define __qadd16	_qadd16

int16x2_t  __BUILTIN _qaddsubx(int16x2_t x, int16x2_t y);
#define __qasx		_qaddsubx

int16x2_t  __BUILTIN _qsubaddx(int16x2_t x, int16x2_t y);
#define __qsax		_qsubaddx

int16x2_t  __BUILTIN _qsub16(int16x2_t x, int16x2_t y);
#define __qsub16	_qsub16

int16x2_t  __BUILTIN _sadd16(int16x2_t x, int16x2_t y);
#define __sadd16	_sadd16

int16x2_t  __BUILTIN _saddsubx(int16x2_t x, int16x2_t y);
#define __sasx		_saddsubx

int16x2_t  __BUILTIN _shadd16(int16x2_t x, int16x2_t y);
#define __shadd16	_shadd16

int16x2_t  __BUILTIN _shaddsubx(int16x2_t x, int16x2_t y);
#define __shasx		_shaddsubx

int16x2_t  __BUILTIN _shsubaddx(int16x2_t x, int16x2_t y);
#define __shsax		_shsubaddx

int16x2_t  __BUILTIN _shsub16(int16x2_t x, int16x2_t y);
#define __shsub16	_shsub16

int16x2_t  __BUILTIN _ssubaddx(int16x2_t x, int16x2_t y);
#define __ssax		_ssubaddx

int16x2_t  __BUILTIN _ssub16(int16x2_t x, int16x2_t y);
#define __ssub16	_ssub16

uint16x2_t __BUILTIN _uadd16(uint16x2_t x, uint16x2_t y);
#define __uadd16	_uadd16

uint16x2_t __BUILTIN _uaddsubx(uint16x2_t x, uint16x2_t y);
#define __uasx		_uaddsubx

uint16x2_t __BUILTIN _uhadd16(uint16x2_t x, uint16x2_t y);
#define __uhadd16	_uhadd16

uint16x2_t __BUILTIN _uhaddsubx(uint16x2_t x, uint16x2_t y);
#define __uhasx		_uhaddsubx

uint16x2_t __BUILTIN _uhsubaddx(uint16x2_t x, uint16x2_t y);
#define __uhsax		_uhsubaddx

uint16x2_t __BUILTIN _uhsub16(uint16x2_t x, uint16x2_t y);
#define __uhsub16	_uhsub16

uint16x2_t __BUILTIN _uqadd16(uint16x2_t x, uint16x2_t y);
#define __uqadd16	_uqadd16

uint16x2_t __BUILTIN _uqaddsubx(uint16x2_t x, uint16x2_t y);
#define __uqasx		_uqaddsubx

uint16x2_t __BUILTIN _uqsubaddx(uint16x2_t x, uint16x2_t y);
#define __uqsax		_uqsubaddx

uint16x2_t __BUILTIN _uqsub16(uint16x2_t x, uint16x2_t y);
#define __uqsub16	_uqsub16

uint16x2_t __BUILTIN _usubaddx(uint16x2_t x, uint16x2_t y);
#define __usax	_usubaddx

uint16x2_t __BUILTIN _usub16(uint16x2_t x, uint16x2_t y);
#define __usub16	_usub16

/*****************************************************************************/
/* Parallel 16-Bit Multiplication (Section 9.5.10)                           */
/* Note: argument order is different between ACLE version of the syntax for  */
/*       these intrinsics and the versions that are already supported in the */
/*       TI ARM compiler.                                                    */
/*****************************************************************************/
int32_t __BUILTIN _smlad(int32_t z, int16x2_t x, int16x2_t y);
#define __smlad(x, y, i32)	_smlad(i32, x, y)

int32_t __BUILTIN _smladx(int32_t z, int16x2_t x, int16x2_t y);
#define __smladx(x, y, i32)	_smladx(i32, x, y)

int64_t __BUILTIN _smlald(int64_t z, int16x2_t x, int16x2_t y);
#define __smlald(x, y, i64)	_smlald(i64, x, y)

int64_t __BUILTIN _smlaldx(int64_t z, int16x2_t x, int16x2_t y);
#define __smlaldx(x, y, i64)	_smlaldx(i64, x, y)

int32_t __BUILTIN _smlsd(int32_t z, int16x2_t x, int16x2_t y);
#define __smlsd(x, y, i32)	_smlsd(i32, x, y)

int32_t __BUILTIN _smlsdx(int32_t z, int16x2_t x, int16x2_t y);
#define __smlsdx(x, y, i32)	_smlsdx(i32, x, y)

int64_t __BUILTIN _smlsld(int64_t z, int16x2_t x, int16x2_t y);
#define __smlsld(x, y, i64)	_smlsld(i64, x, y)

int64_t __BUILTIN _smlsldx(int64_t z, int16x2_t x, int16x2_t y);
#define __smlsldx(x, y, i64)	_smlsldx(i64, x, y)

int32_t __BUILTIN _smuad(int16x2_t x, int16x2_t y);
#define __smuad		_smuad

int32_t __BUILTIN _smuadx(int16x2_t x, int16x2_t y);
#define __smuadx	_smuadx

int32_t __BUILTIN _smusd(int16x2_t x, int16x2_t y);
#define __smusd		_smusd

int32_t __BUILTIN _smusdx(int16x2_t x, int16x2_t y);
#define __smusdx	_smusdx

#endif /* defined(__ARM_FEATURE_DSP) */

/*---------------------------------------------------------------------------*/
/* Floating-Point Data-Processing Intrinsics (Section 9.6)                   */
/*---------------------------------------------------------------------------*/
/* SQRT Intrinsics are not supported in TI ARM Compiler
** double __BUILTIN __sqrt(double x);
** float  __BUILTIN __sqrtf(float x);
*/

/* Double version of FMA Intrinsic is not supported in TI ARM Compiler
** double __BUILTIN __fma(double x, double y, double z);
*/

float  __BUILTIN fmaf(float x, float y, float z);
#define __fmaf(x, y, z)		fmaf(x, y, z)

/* RINTN intrinsics are not supported in the TI ARM Compiler
** float  __BUILTIN __rintnf(float x);
** double __BUILTIN __rintn (double x);
*/

/*---------------------------------------------------------------------------*/
/* CRC32 Intrinsics (Section 9.7)                                            */
/*---------------------------------------------------------------------------*/
/* CRC32 instructions are available in ARMv8-A architecture, not Cortex-M
** uint32_t __BUILTIN __crc32b(uint32_t a, uint8_t b);
** uint32_t __BUILTIN __crc32h(uint32_t a, uint16_t b);
** uint32_t __BUILTIN __crc32w(uint32_t a, uint32_t b);
** uint32_t __BUILTIN __crc32d(uint32_t a, uint64_t b);
** uint32_t __BUILTIN __crc32cb(uint32_t a, uint8_t b);
** uint32_t __BUILTIN __crc32ch(uint32_t a, uint16_t b);
** uint32_t __BUILTIN __crc32cw(uint32_t a, uint32_t b);
** uint32_t __BUILTIN __crc32cd(uint32_t a, uint64_t b);
*/

/*===========================================================================*/
/* System Register Access (Chapter 10)                                       */
/*===========================================================================*/
/* These intrinsics are not supported in TI ARM Compiler
** uint32_t __BUILTIN __arm_rsr(const char *special_register);
** uint32_t __BUILTIN __arm_rsr64(const char *special_register);
** void *   __BUILTIN __arm_rsrp(const char *special_register);
** void     __BUILTIN __arm_wsr(const char *special_register, uint32_t value);
** void     __BUILTIN __arm_wsr64(const char *special_register, uint64_t value);
** void     __BUILTIN __arm_wsrp(const char *special_register, const void *value);
*/

/*===========================================================================*/
/* Coprocessor Instruction Intrinsics                                        */
/*===========================================================================*/

#if !defined(__TI_ARM_V6M0__)
#if __ARM_ARCH >= 4
/*****************************************************************************/
/* __arm_cdp() - Coprocessor Data Processing intrinsic.                      */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the CDP coprocessor 
** instructions
** void __BUILTIN_M
** __arm_cdp(__CONST(0, 15) unsigned int __coproc, 
**           __CONST(0, 15) unsigned int __opc1, 
**           __CONST(0, 15) unsigned int __CRd,
**           __CONST(0, 15) unsigned int __CRn, 
**           __CONST(0, 15) unsigned int __CRm,
**           __CONST(0, 7)  unsigned int __opc2);
*/

/*****************************************************************************/
/* __arm_ldc() - Load Coprocessor intrinsic.                                 */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the CDP coprocessor 
** instructions
** void __BUILTIN_M
** __arm_ldc(__CONST(0, 15) unsigned int __coproc,
**           __CONST(0, 15) unsigned int __CRd,
**           const void                 *__p);
*/

/*****************************************************************************/
/* __arm_ldcl() - Load Coprocessor (D == 1) intrinsic.                       */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the LDC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_ldcl(__CONST(0, 15) unsigned int __coproc,
**            __CONST(0, 15) unsigned int __CRd,
**            const void                 *__p);
*/

/*****************************************************************************/
/* __arm_stc() - Store Coprocessor intrinsic.                                */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the STC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_stc(__CONST(0, 15) unsigned int __coproc,
**           __CONST(0, 15) unsigned int __CRd,
**           const void                 *__p);
*/

/*****************************************************************************/
/* __arm_stcl() - Store Coprocessor (D == 1) intrinsic.                      */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the STC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_stcl(__CONST(0, 15) unsigned int __coproc,
**            __CONST(0, 15) unsigned int __CRd,
**            const void                 *__p);
*/

/*****************************************************************************/
/* Previous versions of the TI ARM compiler already support intrinsics that  */
/* map to MRC, MCR, MRRC, and MCRR instructions, so the ACLE forms of these  */
/* intrinsics will simply inline the existing instruction intrinsics.        */
/*****************************************************************************/
/* __arm_mcr() - Move Coprocessor to Register intrinsic.                     */
/*****************************************************************************/
void __BUILTIN_M
__MCR(__CONST(0, 15) unsigned int __coproc,
      __CONST(0, 7)  unsigned int __opc1, 
      uint32_t                    __value,
      __CONST(0, 15) unsigned int __CRn,
      __CONST(0, 15) unsigned int __CRm,
      __CONST(0, 7)  unsigned int __opc2);
#define __arm_mcr	__MCR

/*****************************************************************************/
/* __arm_mrc() - Move Register to Coprocessor intrinsic.                     */
/*****************************************************************************/
uint32_t __BUILTIN_M
__MRC(__CONST(0, 15) unsigned int __coproc,
      __CONST(0, 7)  unsigned int __opc1, 
      __CONST(0, 15) unsigned int __CRn,
      __CONST(0, 15) unsigned int __CRm,
      __CONST(0, 7)  unsigned int __opc2);
#define __arm_mrc	__MRC

#if __ARM_ARCH >= 5
/*****************************************************************************/
/* __arm_cdp2() - Coprocessor Data Processing intrinsic.                     */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the CDP coprocessor 
** instructions
** void __BUILTIN_M
** __arm_cdp2(__CONST(0, 15) unsigned int __coproc, 
**            __CONST(0, 15) unsigned int __opc1, 
**            __CONST(0, 15) unsigned int __CRd,
**            __CONST(0, 15) unsigned int __CRn, 
**            __CONST(0, 15) unsigned int __CRm,
**            __CONST(0, 7)  unsigned int __opc2);
*/

/*****************************************************************************/
/* __arm_ldc2() - Load Coprocessor intrinsic.                                */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the LDC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_ldc2(__CONST(0, 15) unsigned int __coproc,
**            __CONST(0, 15) unsigned int __CRd,
**            const void                 *__p);
*/

/*****************************************************************************/
/* __arm_ldc2l() - Load Coprocessor (D == 1) intrinsic.                      */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the LDC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_ldc2l(__CONST(0, 15) unsigned int __coproc,
**             __CONST(0, 15) unsigned int __CRd,
**             const void                 *__p);
*/

/*****************************************************************************/
/* __arm_stc2() - Store Coprocessor intrinsic.                               */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the STC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_stc2(__CONST(0, 15) unsigned int __coproc,
**            __CONST(0, 15) unsigned int __CRd,
**            const void                 *__p);
*/

/*****************************************************************************/
/* __arm_stc2l() - Store Coprocessor (D == 1) intrinsic.                     */
/*****************************************************************************/
/* The TI ARM Compiler does not support intrinsics for the STC coprocessor 
** instructions
** void __BUILTIN_M
** __arm_stc2l(__CONST(0, 15) unsigned int __coproc,
**             __CONST(0, 15) unsigned int __CRd,
**             const void                 *__p);
*/

/*****************************************************************************/
/* Previous versions of the TI ARM compiler already support intrinsics that  */
/* map to MRC, MCR, MRRC, and MCRR instructions, so the ACLE forms of these  */
/* intrinsics will simply inline the existing instruction intrinsics.        */
/*****************************************************************************/
/*****************************************************************************/
/* __arm_mcr2() - Move Coprocessor to Register intrinsic.                    */
/*****************************************************************************/
void __BUILTIN_M
__MCR2(__CONST(0, 15) unsigned int __coproc,
       __CONST(0, 7)  unsigned int __opc1, 
       uint32_t                    __value,
       __CONST(0, 15) unsigned int __CRn,
       __CONST(0, 15) unsigned int __CRm,
       __CONST(0, 7)  unsigned int __opc2);
#define __arm_mcr2	__MCR2

/*****************************************************************************/
/* __arm_mrc2() - Move Register to Coprocessor intrinsic.                    */
/*****************************************************************************/
uint32_t __BUILTIN_M
__MRC2(__CONST(0, 15) unsigned int __coproc,
       __CONST(0, 7)  unsigned int __opc1, 
       __CONST(0, 15) unsigned int __CRn,
       __CONST(0, 15) unsigned int __CRm,
       __CONST(0, 7)  unsigned int __opc2);
#define __arm_mrc2	__MRC2

/*****************************************************************************/
/* __arm_mcrr() - Move Two Registers to Coprocessor intrinsic.               */
/*****************************************************************************/
void __BUILTIN_M
__MCRR(__CONST(0, 15) unsigned int __coproc,
       __CONST(0, 15) unsigned int __opc1, 
       uint64_t                    __value,
       __CONST(0, 15) unsigned int __CRm);
#define __arm_mcrr	__MCRR

/*****************************************************************************/
/* __arm_mcrr2() - Move Two Registers to Coprocessor intrinsic.              */
/*****************************************************************************/
void __BUILTIN_M
__MCRR2(__CONST(0, 15) unsigned int __coproc,
        __CONST(0, 15) unsigned int __opc1, 
        uint64_t                    __value,
	__CONST(0, 15) unsigned int __CRm);
#define __arm_mcrr2	__MCRR2

/*****************************************************************************/
/* __arm_mrrc() - Move Coprocessor to Two Registers intrinsic.               */
/*****************************************************************************/
uint64_t __BUILTIN_M
__MRRC(__CONST(0, 15) unsigned int __coproc,
       __CONST(0, 15) unsigned int __opc1, 
       __CONST(0, 15) unsigned int __CRm);
#define __arm_mrrc	__MRRC

/*****************************************************************************/
/* __arm_mrrc2() - Move Coprocessor to Two Registers intrinsic.              */
/*****************************************************************************/
uint64_t __BUILTIN_M
__MRRC2(__CONST(0, 15) unsigned int __coproc,
        __CONST(0, 15) unsigned int __opc1, 
        __CONST(0, 15) unsigned int __CRm);
#define __arm_mrrc2	__MRRC2

#endif /* __ARM_ARCH >= 5 */
#endif /* __ARM_ARCH >= 4 */
#endif /* !defined(__TI_ARM_V6M0__) */

#ifdef __cplusplus
   }
#endif  /* extern "C" */

#endif /* _TI_ARM_ACLE_H */
