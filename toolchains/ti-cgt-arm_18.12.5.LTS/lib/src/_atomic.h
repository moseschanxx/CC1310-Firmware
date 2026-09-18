/******************************************************************************/
/*                                                                            */
/* _ATOMIC.H                                                                  */
/* Copyright (c) 2018 Texas Instruments Incorporated                          */
/* http://www.ti.com/                                                         */
/*                                                                            */
/*  Redistribution and  use in source  and binary forms, with  or without     */
/*  modification,  are permitted provided  that the  following conditions     */
/*  are met:                                                                  */
/*                                                                            */
/*     Redistributions  of source  code must  retain the  above copyright     */
/*     notice, this list of conditions and the following disclaimer.          */
/*                                                                            */
/*     Redistributions in binary form  must reproduce the above copyright     */
/*     notice, this  list of conditions  and the following  disclaimer in     */
/*     the  documentation  and/or   other  materials  provided  with  the     */
/*     distribution.                                                          */
/*                                                                            */
/*     Neither the  name of Texas Instruments Incorporated  nor the names     */
/*     of its  contributors may  be used to  endorse or  promote products     */
/*     derived  from   this  software  without   specific  prior  written     */
/*     permission.                                                            */
/*                                                                            */
/*  THIS SOFTWARE  IS PROVIDED BY THE COPYRIGHT  HOLDERS AND CONTRIBUTORS     */
/*  "AS IS"  AND ANY  EXPRESS OR IMPLIED  WARRANTIES, INCLUDING,  BUT NOT     */
/*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     */
/*  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT     */
/*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     */
/*  SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES  (INCLUDING, BUT  NOT     */
/*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     */
/*  THEORY OF  LIABILITY, WHETHER IN CONTRACT, STRICT  LIABILITY, OR TORT     */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     */
/*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
#ifndef __ATOMICHDR
#define __ATOMICHDR
#ifndef __cplusplus
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#else
#include <cstdlib>
#include <cstdint>
#include <cstdbool>
#endif

/* Implement the GCC Atomic builtins required to implement C++1x */

#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

#ifdef __ARM_FEATURE_LDREX
#define __ATOMIC_ALWAYS_LOCK_FREE(n) \
    (((n) == 1 ? (__ARM_FEATURE_LDREX & 0x01) : \
      (n) == 2 ? (__ARM_FEATURE_LDREX & 0x02) : \
      (n) == 4 ? (__ARM_FEATURE_LDREX & 0x04) : \
      (n) == 8 ? (__ARM_FEATURE_LDREX & 0x08) : 0) != 0)
#else
/* Using disable/restore interrupts */
#define __ATOMIC_ALWAYS_LOCK_FREE(n) \
    ((n) == 1 || \
     (n) == 2 || \
     (n) == 4 || \
     (n) == 8 )
#endif

#define __GCC_ATOMIC_BOOL_LOCK_FREE       __ATOMIC_ALWAYS_LOCK_FREE(1)
#define __GCC_ATOMIC_CHAR_LOCK_FREE       __ATOMIC_ALWAYS_LOCK_FREE(1)
#define __GCC_ATOMIC_CHAR16_T_LOCK_FREE   __ATOMIC_ALWAYS_LOCK_FREE(2)
#define __GCC_ATOMIC_CHAR32_T_LOCK_FREE   __ATOMIC_ALWAYS_LOCK_FREE(4)
#define __GCC_ATOMIC_WCHAR_T_LOCK_FREE    \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_WCHAR_T__)
#define __GCC_ATOMIC_SHORT_LOCK_FREE      \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_SHORT__)
#define __GCC_ATOMIC_INT_LOCK_FREE        \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_INT__)
#define __GCC_ATOMIC_LONG_LOCK_FREE       \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_LONG__)
#define __GCC_ATOMIC_LLONG_LOCK_FREE      \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_LONG_LONG__)
#ifdef __SIZEOF_POINTER__
#define __GCC_ATOMIC_POINTER_LOCK_FREE    \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_POINTER__)
#else
#define __GCC_ATOMIC_POINTER_LOCK_FREE    \
   __ATOMIC_ALWAYS_LOCK_FREE(__SIZEOF_PTRDIFF_T__)
#endif

static inline bool 
__atomic_always_lock_free(size_t n, const void *ptr)
{
  return __ATOMIC_ALWAYS_LOCK_FREE(n);
}

static inline bool 
__atomic_is_lock_free(size_t n, const void *ptr)
{
  return __ATOMIC_ALWAYS_LOCK_FREE(n);
}

#define __TI_ATOMIC_FUNC(type, name) \
__attribute__((always_inline)) static inline type name

#ifdef __ARM_FEATURE_LDREX

#define __atomic_thread_fence(order) _dmb()
#define __atomic_signal_fence(order) _dmb()

#ifdef __ATOMIC_ALWAYS_SEQ_CST
#define __ti_release_fence(order) __atomic_thread_fence(order)
#define __ti_acquire_fence(order) __atomic_thread_fence(order)
#else
#define __ti_release_fence(order) \
do { \
  switch (order) { \
    case __ATOMIC_RELEASE: \
    case __ATOMIC_ACQ_REL: \
    case __ATOMIC_SEQ_CST:  \
      __atomic_thread_fence(order); \
      break; \
    case __ATOMIC_RELAXED: \
    case __ATOMIC_CONSUME: \
    case __ATOMIC_ACQUIRE: \
    default:  \
      break; \
  } \
} while (0)

#define __ti_acquire_fence(order) \
do { \
  switch (order) { \
    case __ATOMIC_ACQUIRE: \
    case __ATOMIC_ACQ_REL: \
    case __ATOMIC_SEQ_CST: \
      __atomic_thread_fence(order); \
      break; \
    case __ATOMIC_RELAXED: \
    case __ATOMIC_CONSUME: \
    case __ATOMIC_RELEASE: \
    default: \
      break; \
  } \
} while (0)
#endif

#define __LDREX1 __ldrexb
#define __LDREX2 __ldrexh
#define __LDREX4 __ldrex
#define __LDREX8 __ldrexd

#define __STREX1 __strexb
#define __STREX2 __strexh
#define __STREX4 __strex
#define __STREX8 __strexd

#define __CLREX __clrex

/* __atomic_load_[1,2,4]() builtins */

#define __TI_ATOMIC_LOAD_N(n, type) \
__TI_ATOMIC_FUNC(type, __atomic_load_##n) \
(const void *ptr, int memorder) \
{ \
  /* __ti_release_fence(memorder); */ \
  type t0 = *(const volatile type*)ptr; \
  __ti_acquire_fence(memorder); \
  return t0; \
}

/* __atomic_load_8() builtin */

#define __TI_ATOMIC_LOAD_8(type) \
__TI_ATOMIC_FUNC(type, __atomic_load_8) \
(const void *ptr, int memorder) \
{ \
  /* __ti_release_fence(memorder); */ \
  type t0 = __LDREX8((volatile void*)ptr); \
  __CLREX(); \
  __ti_acquire_fence(memorder); \
  return t0; \
}

/* __atomic_store_[1,2,4]() builtins */

#define __TI_ATOMIC_STORE_N(n, type) \
__TI_ATOMIC_FUNC(void, __atomic_store_##n) \
(void *ptr, type val, int memorder) \
{ \
  __ti_release_fence(memorder); \
  *(volatile type*)ptr = val; \
  __ti_acquire_fence(memorder); \
  return; \
}

/* __atomic_store_8() builtin */

#define __TI_ATOMIC_STORE_8(type) \
__TI_ATOMIC_FUNC(void, __atomic_store_8) \
(void *ptr, type val, int memorder) \
{ \
  __ti_release_fence(memorder); \
  uint32_t exflag; \
  retry: \
    (void)__LDREX8(ptr); \
    exflag = __STREX8(val, ptr); \
  if (exflag) goto retry; \
  __ti_acquire_fence(memorder); \
  return; \
}

/* __atomic_exchange_[1,2,4,8]() builtins */

#define __TI_ATOMIC_EXCHANGE_N(n, type) \
__TI_ATOMIC_FUNC(type, __atomic_exchange_##n) \
(void *ptr, type val, int memorder) \
{ \
  type t0; \
  uint32_t exflag; \
  __ti_release_fence(memorder); \
  retry: \
    t0 = __LDREX##n(ptr); \
    exflag = __STREX##n(val, ptr); \
  if (exflag) goto retry; \
  __ti_acquire_fence(memorder); \
  return t0; \
}

/* __atomic_compare_exchange_[1,2,4,8] builtins */
/* Match libatomic - drop the (4th parameter) strong/weak flag */

#define __TI_ATOMIC_COMPARE_EXCHANGE_N(n, type) \
__TI_ATOMIC_FUNC(bool, __atomic_compare_exchange_##n) \
(void *ptr, void *expected, type desired, /* bool weak,*/ \
  int success_memorder, int failure_memorder) \
{ \
  type t0, t1; \
  uint32_t exflag = 0; \
  __ti_release_fence(success_memorder); \
  t1 = *(type*)expected; \
  retry:\
  t0 = __LDREX##n(ptr); \
  if (t0 == t1) {\
    exflag = __STREX##n(desired, ptr); \
    if (exflag) goto retry; \
    __ti_acquire_fence(success_memorder); \
    return 1;\
  } else { \
    __CLREX(); \
    *(type*)expected = t0; \
    __ti_acquire_fence(failure_memorder); \
    return 0; \
  }\
}

/* __atomic_fetch_[add,sub,add,xor,or][1,2,4,8] builtins */

#define __TI_ATOMIC_RMW(n, type, opname, op) \
__TI_ATOMIC_FUNC(type, __atomic_##opname##_##n) \
(void *ptr, type val, int memorder) \
{ \
  type t0, t1; \
  uint32_t exflag = 0; \
  __ti_release_fence(memorder); \
  retry: \
    t0 = __LDREX##n(ptr); \
    t1 = t0 op val; \
    exflag = __STREX##n(t1, ptr); \
  if (exflag) goto retry; \
  __ti_acquire_fence(memorder); \
  return t0;\
}

/*------------------------------------------------------------*/
/* V4, V5e, V6 and V6-M0                                      */
/* no LDREX/SRTEX/CLREX - disable/restore interrupts          */
/*------------------------------------------------------------*/
#else

#if defined(__TI_ARM_V6M0__)
#define __atomic_thread_fence(order) _dmb()
#define __atomic_signal_fence(order) _dmb()
#define __disable_interrupts _disable_interrupts
#define __restore_interrupts _restore_interrupts
#else
#define __atomic_thread_fence(order)
#define __atomic_signal_fence(order)
#if !defined(__ARM_32BIT_STATE)
#define __ATTRIBUTE_NOINLINE__ __attribute__((noinline))
#else
#define __ATTRIBUTE_NOINLINE__ 
#endif
__attribute__((target("arm"))) __ATTRIBUTE_NOINLINE__
static uint32_t __disable_interrupts() { return _disable_interrupts(); }
__attribute__((target("arm"))) __ATTRIBUTE_NOINLINE__
static void __restore_interrupts(uint32_t t) { _restore_interrupts(t); }
#undef __ATTRIBUTE_NOINLINE__
#endif

/* __atomic_load_[1,2,4,8]() builtins */

#define __TI_ATOMIC_LOAD_N(n, type) \
__TI_ATOMIC_FUNC(type, __atomic_load_##n) \
(const void *ptr, int memorder) \
{ \
  uint32_t tmp = __disable_interrupts(); \
  type t0 = *(const volatile type*)ptr; \
  __restore_interrupts(tmp); \
  return t0; \
}

/* __atomic_store_[1,2,4,8]() builtins */

#define __TI_ATOMIC_STORE_N(n, type) \
__TI_ATOMIC_FUNC(void, __atomic_store_##n) \
(void *ptr, type val, int memorder) \
{ \
  uint32_t tmp = __disable_interrupts(); \
  *(volatile type*)ptr = val; \
  __restore_interrupts(tmp); \
  return; \
}

/* __atomic_exchange_[1,2,4,8]() builtins */

#define __TI_ATOMIC_EXCHANGE_N(n, type) \
__TI_ATOMIC_FUNC(type, __atomic_exchange_##n) \
(void *ptr, type val, int memorder) \
{ \
  uint32_t tmp = __disable_interrupts(); \
  type t0 = *(volatile type*)ptr; \
  *(volatile type*)ptr = val; \
  __restore_interrupts(tmp); \
  return t0; \
}

/* __atomic_compare_exchange_[1,2,4,8] builtins */
/* Match libatomic - drop the (4th parameter) strong/weak flag */

#define __TI_ATOMIC_COMPARE_EXCHANGE_N(n, type) \
__TI_ATOMIC_FUNC(bool, __atomic_compare_exchange_##n) \
(void *ptr, void *expected, type desired, /* bool weak,*/ \
 int success_memorder, int failure_memorder) \
{ \
  type t1 = *(type*)expected; \
  uint32_t tmp = __disable_interrupts(); \
  type t0 = *(volatile type*)ptr; \
  if (t0 == t1) {\
    *(volatile type*)ptr = desired; \
    __restore_interrupts(tmp); \
    return 1;\
  } else { \
    *(type*)expected = t0; \
    __restore_interrupts(tmp); \
    return 0; \
  }\
}

/* __atomic_fetch_[add,sub,add,xor,or][1,2,4,8] builtins */

#define __TI_ATOMIC_RMW(n, type, opname, op) \
__TI_ATOMIC_FUNC(type, __atomic_##opname##_##n) \
(void *ptr, type val, int memorder) \
{ \
  type t0, t1; \
  uint32_t tmp = __disable_interrupts(); \
  t0 = *(volatile type*)ptr; \
  t1 = t0 op val; \
  *(volatile type*)ptr = t1; \
  __restore_interrupts(tmp); \
  return t0;\
}
#endif

/* Define lock free atomic builtins */

#define __TI_ATOMIC_N(n, type) \
__TI_ATOMIC_EXCHANGE_N(n, type) \
__TI_ATOMIC_COMPARE_EXCHANGE_N(n, type) \
__TI_ATOMIC_RMW(n, type, fetch_add, +) \
__TI_ATOMIC_RMW(n, type, fetch_sub, -) \
__TI_ATOMIC_RMW(n, type, fetch_and, &) \
__TI_ATOMIC_RMW(n, type, fetch_xor, ^) \
__TI_ATOMIC_RMW(n, type, fetch_or,  |)

#if __ATOMIC_ALWAYS_LOCK_FREE(1)
__TI_ATOMIC_LOAD_N(1, uint8_t)
__TI_ATOMIC_STORE_N(1, uint8_t)
__TI_ATOMIC_N(1, uint8_t)
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(2)
__TI_ATOMIC_LOAD_N(2, uint16_t) 
__TI_ATOMIC_STORE_N(2, uint16_t)
__TI_ATOMIC_N(2, uint16_t)
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(4)
__TI_ATOMIC_LOAD_N(4, uint32_t)
__TI_ATOMIC_STORE_N(4, uint32_t)
__TI_ATOMIC_N(4, uint32_t)
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(8)
#if __ARM_FEATURE_LDREX
__TI_ATOMIC_LOAD_8(uint64_t)
__TI_ATOMIC_STORE_8(uint64_t)
#else
__TI_ATOMIC_LOAD_N(8, uint64_t)
__TI_ATOMIC_STORE_N(8, uint64_t)
#endif
__TI_ATOMIC_N(8, uint64_t)
#endif

/* Match libatomic - drop the (4th parameter) strong/weak flag */
#define __atomic_compare_exchange(ptr, expected, desired, weak, suc, fail)\
   __atomic_compare_exchange(ptr, expected, desired, suc, fail)

#define __atomic_compare_exchange_n(ptr, expected, desired, weak, suc, fail)\
   __atomic_compare_exchange_n(ptr, expected, desired, suc, fail)

#undef __ti_release_fence
#undef __ti_aqcuire_fence
#undef __TI_ATOMIC_LOAD_N
#undef __TI_ATOMIC_LOAD_8
#undef __TI_ATOMIC_STORE_N
#undef __TI_ATOMIC_STORE_8
#undef __TI_ATOMIC_N
#undef __TI_ATOMIC_EXCHANGE_N
#undef __TI_ATOMIC_COMPARE_EXCHANGE_N
#undef __TI_ATOMIC_RMW
#undef __LDREX1
#undef __LDREX2
#undef __LDREX4
#undef __LDREX8
#undef __STREX1
#undef __STREX2
#undef __STREX4
#undef __STREX8
#undef __CLREX
#undef __TI_ATOMIC_FUNC
#undef __disable_interrupts
#undef __restore_interrupts

#endif /* __ATOMICHDR */
