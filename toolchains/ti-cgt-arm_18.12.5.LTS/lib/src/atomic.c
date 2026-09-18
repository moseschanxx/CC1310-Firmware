/****************************************************************************/
/*  ATOMIC.C                                                                */
/*                                                                          */
/* Copyright (c) 2018 Texas Instruments Incorporated                        */
/* http://www.ti.com/                                                       */
/*                                                                          */
/*  Redistribution and  use in source  and binary forms, with  or without   */
/*  modification,  are permitted provided  that the  following conditions   */
/*  are met:                                                                */
/*                                                                          */
/*     Redistributions  of source  code must  retain the  above copyright   */
/*     notice, this list of conditions and the following disclaimer.        */
/*                                                                          */
/*     Redistributions in binary form  must reproduce the above copyright   */
/*     notice, this  list of conditions  and the following  disclaimer in   */
/*     the  documentation  and/or   other  materials  provided  with  the   */
/*     distribution.                                                        */
/*                                                                          */
/*     Neither the  name of Texas Instruments Incorporated  nor the names   */
/*     of its  contributors may  be used to  endorse or  promote products   */
/*     derived  from   this  software  without   specific  prior  written   */
/*     permission.                                                          */
/*                                                                          */
/*  THIS SOFTWARE  IS PROVIDED BY THE COPYRIGHT  HOLDERS AND CONTRIBUTORS   */
/*  "AS IS"  AND ANY  EXPRESS OR IMPLIED  WARRANTIES, INCLUDING,  BUT NOT   */
/*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR   */
/*  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT   */
/*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,   */
/*  SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES  (INCLUDING, BUT  NOT   */
/*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,   */
/*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY   */
/*  THEORY OF  LIABILITY, WHETHER IN CONTRACT, STRICT  LIABILITY, OR TORT   */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE   */
/*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.    */
/*                                                                          */
/****************************************************************************/

// This file was modified from the original version in LLVM

/*===-- atomic.c - Implement support functions for atomic operations.------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===
 *
 *  atomic.c defines a set of functions for performing atomic accesses on
 *  arbitrary-sized memory locations.  This design uses locks that should
 *  be fast in the uncontended case, for two reasons:
 * 
 *  1) This code must work with C programs that do not link to anything
 *     (including pthreads) and so it should not depend on any pthread
 *     functions.
 *  2) Atomic operations, rather than explicit mutexes, are most commonly used
 *     on code where contended operations are rate.
 * 
 *  To avoid needing a per-object lock, this code allocates an array of
 *  locks and hashes the object pointers to find the one that it should use.
 *  For operations that must be atomic on two locations, the lower lock is
 *  always acquired first, to avoid deadlock.
 *
 *===----------------------------------------------------------------------===
 */

#ifndef __STDC_NO_ATOMICS__

#include <stdint.h>
#include <string.h>
#include <_atomic.h>

// The parser objects if you redefine a builtin.  This little hack allows us to
// define a function with the same name as an intrinsic.
#pragma redefine_extname __atomic_load_c __atomic_load
#pragma redefine_extname __atomic_store_c __atomic_store
#pragma redefine_extname __atomic_exchange_c __atomic_exchange
#pragma redefine_extname __atomic_compare_exchange_c __atomic_compare_exchange


/// Default to 32 locks.  
/// This can be specified externally if a different trade between
/// memory usage and contention probability is required for a given platform.
#ifndef SPINLOCK_COUNT
#define SPINLOCK_COUNT (1<<5)
#endif
static const long SPINLOCK_MASK = SPINLOCK_COUNT - 1;

////////////////////////////////////////////////////////////////////////////////
// Lock implementation using spinlocks.
// TODO: Update this to be overriden by an RTOS provided implementation of 
// lock() and unlock() functions.
////////////////////////////////////////////////////////////////////////////////
typedef uintptr_t Lock;
/// Unlock a lock.  This is a release operation.
__inline static void unlock(Lock *l) {
  __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}
/// Locks a lock.  In the current implementation, this is potentially
/// unbounded in the contended case.
__inline static void lock(Lock *l) {
  uintptr_t old = 0;
  while (!__atomic_compare_exchange_n(l, &old, 1, 1, __ATOMIC_ACQUIRE,
        __ATOMIC_RELAXED))
    old = 0;
}
/// locks for atomic operations
static Lock locks[SPINLOCK_COUNT];


/// Returns a lock to use for a given pointer.  
static __inline Lock *lock_for_pointer(void *ptr) {
  intptr_t hash = (intptr_t)ptr;
  // Disregard the lowest 4 bits.  We want all values that may be part of the
  // same memory operation to hash to the same value and therefore use the same
  // lock.  
  hash >>= 4;
  // Use the next bits as the basis for the hash
  intptr_t low = hash & SPINLOCK_MASK;
  // Now use the high(er) set of bits to perturb the hash, so that we don't
  // get collisions from atomic fields in a single object
#if  defined(__MSP430__)
  hash >>= 8;
#else
  hash >>= 16;
#endif
  hash ^= low;
  // Return a pointer to the word to use
  return locks + (hash & SPINLOCK_MASK);
}

/// Macros for determining whether a size is lock free.  Clang can not yet
/// codegen __atomic_is_lock_free(16), so for now we assume 16-byte values are
/// not lock free.
#define TOSTR(a) #a

#ifndef TYPE1
#define TYPE1 uint8_t
#define TYPE2 uint16_t
#define TYPE4 uint32_t
#define TYPE8 uint64_t
#define TYPE16 __uint128_t
#endif

#define LOCK_FREE_CASE(n) \
    do { \
      if (__atomic_always_lock_free(n, 0)) {\
        LOCK_FREE_ACTION(n);\
      } \
    } while (0)

#if __ATOMIC_ALWAYS_LOCK_FREE(1)
#define LOCK_FREE_CASE_1 LOCK_FREE_CASE(1)
#else
#define LOCK_FREE_CASE_1 break
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(2)
#define LOCK_FREE_CASE_2 LOCK_FREE_CASE(2)
#else
#define LOCK_FREE_CASE_2 break
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(4)
#define LOCK_FREE_CASE_4 LOCK_FREE_CASE(4)
#else
#define LOCK_FREE_CASE_4 break
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(8)
#define LOCK_FREE_CASE_8 LOCK_FREE_CASE(8)
#else
#define LOCK_FREE_CASE_8 break
#endif
#if __ATOMIC_ALWAYS_LOCK_FREE(16)
#define LOCK_FREE_CASE_16 LOCK_FREE_CASE(16)
#else
#define LOCK_FREE_CASE_16 break
#endif

/// Macro that calls the compiler-generated lock-free versions of functions
/// when they exist.
#define LOCK_FREE_CASES() \
  do {\
  switch (size) {\
    case  1: LOCK_FREE_CASE_1; \
    case  2: LOCK_FREE_CASE_2; \
    case  4: LOCK_FREE_CASE_4; \
    case  8: LOCK_FREE_CASE_8; \
    case 16: LOCK_FREE_CASE_16; ;\
    default: break;\
  }\
  } while (0)

/// An atomic load operation.  This is atomic with respect to the source
/// pointer only.

void __atomic_load_c(size_t size, const void *src, void *dest, int model) {
#define LOCK_FREE_ACTION(n) \
    *((TYPE##n*)dest) = __atomic_load_n((TYPE##n *)src, model);\
    return
  LOCK_FREE_CASES();
#undef LOCK_FREE_ACTION
  void *_src = (void*)src;
  Lock *l = lock_for_pointer(_src);
  lock(l);
  memcpy(dest, _src, size);
  unlock(l);
}

/// An atomic store operation.  This is atomic with respect to the destination
/// pointer only.
void __atomic_store_c(size_t size, void *dest, void *src, int model) {
#define LOCK_FREE_ACTION(n) \
    __atomic_store_n((TYPE##n*)dest, *(TYPE##n*)src, model);\
    return
  LOCK_FREE_CASES();
#undef LOCK_FREE_ACTION
  void *_dest = (void*)dest;
  Lock *l = lock_for_pointer(_dest);
  lock(l);
  memcpy(_dest, src, size);
  unlock(l);
}

/// Atomic compare and exchange operation.  If the value at *ptr is identical
/// to the value at *expected, then this copies value at *desired to *ptr.  If
/// they  are not, then this stores the current value from *ptr in *expected.
///
/// This function returns 1 if the exchange takes place or 0 if it fails. 
_Bool __atomic_compare_exchange_c(size_t size, void *ptr, 
      void *expected, void *desired, int success, int failure) {
#define LOCK_FREE_ACTION(n) \
  return __atomic_compare_exchange_n((TYPE##n*)ptr, (TYPE##n*)expected,\
      *(TYPE##n*)desired, 0, success, failure)
  LOCK_FREE_CASES();
#undef LOCK_FREE_ACTION
  void *_ptr = (void*)ptr;
  Lock *l = lock_for_pointer(_ptr);
  lock(l);
  if (memcmp(_ptr, expected, size) == 0) {
    memcpy(_ptr, desired, size);
    unlock(l);
    return 1;
  }
  memcpy(expected, _ptr, size);
  unlock(l);
  return 0;
}

/// Performs an atomic exchange operation between two pointers.  This is atomic
/// with respect to the target address.
void __atomic_exchange_c(size_t size, void *ptr, void *val, 
      void *old, int model) {
#define LOCK_FREE_ACTION(n) \
    *(TYPE##n*)old = __atomic_exchange_n((TYPE##n*)ptr, *(TYPE##n*)val, model);\
    return;
  LOCK_FREE_CASES();
#undef LOCK_FREE_ACTION
  void *_ptr = (void*)ptr;
  Lock *l = lock_for_pointer(_ptr);
  lock(l);
  memcpy(old, _ptr, size);
  memcpy(_ptr, val, size);
  unlock(l);
}

////////////////////////////////////////////////////////////////////////////////
// Where the size is known at compile time, the compiler may emit calls to
// specialised versions of the above functions.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Atomic load for integers of various sizes.
////////////////////////////////////////////////////////////////////////////////

#define __ATOMIC_LOAD_N(n)\
_Pragma( TOSTR(redefine_extname __atomic_load_##n##_c \
			        __atomic_load_##n) ) \
TYPE##n __atomic_load_##n##_c(const void *src, int model) {\
  TYPE##n val;\
  TYPE##n *_src = (TYPE##n*)src; \
  if (__atomic_always_lock_free(n, 0)) {\
    return __atomic_load_n(_src, model);\
  } else {\
    Lock *l = lock_for_pointer(_src);\
    lock(l);\
    val = *_src;\
    unlock(l);\
    return val;\
  }\
}

////////////////////////////////////////////////////////////////////////////////
// Atomic store for integers of various sizes.
////////////////////////////////////////////////////////////////////////////////

#define __ATOMIC_STORE_N(n)\
_Pragma( TOSTR(redefine_extname __atomic_store_##n##_c \
			        __atomic_store_##n) ) \
void  __atomic_store_##n##_c(void *dst, TYPE##n val, int model) {\
  TYPE##n *_dst = (TYPE##n*)dst; \
  if (__atomic_always_lock_free(n, 0)) {\
    __atomic_store_n(_dst, val, model);\
  } else { \
    Lock *l = lock_for_pointer(_dst);\
    lock(l);\
    *_dst = val;\
    unlock(l);\
  } \
  return;\
}

////////////////////////////////////////////////////////////////////////////////
// Atomic exchange for integers of various sizes.
////////////////////////////////////////////////////////////////////////////////

#define __ATOMIC_EXCHANGE_N(n)\
_Pragma( TOSTR(redefine_extname __atomic_exchange_##n##_c \
			        __atomic_exchange_##n) ) \
TYPE##n __atomic_exchange_##n##_c(void *dst, TYPE##n val, int model) {\
  TYPE##n tmp;\
  TYPE##n *_dst = (TYPE##n*)dst; \
  if (__atomic_always_lock_free(n, 0)) {\
    return __atomic_exchange_n(_dst, val, model);\
  } else { \
    Lock *l = lock_for_pointer(_dst);\
    lock(l);\
    tmp = *_dst;\
    *_dst = val;\
    unlock(l);\
    return tmp;\
  } \
}

////////////////////////////////////////////////////////////////////////////////
// Atomic compare-exchange for integers of various sizes.
////////////////////////////////////////////////////////////////////////////////

#define __ATOMIC_COMPARE_EXCHANGE_N(n)\
_Pragma( TOSTR(redefine_extname __atomic_compare_exchange_##n##_c \
			        __atomic_compare_exchange_##n) ) \
_Bool __atomic_compare_exchange_##n##_c(void *ptr, void *expected, \
                                   TYPE##n desired, \
                                   int success, int failure) {\
  TYPE##n *_ptr = (TYPE##n*)ptr; \
  TYPE##n *_expected = (TYPE##n*)expected; \
  int ret = 1; \
  if (__atomic_always_lock_free(n, 0)) {\
    return __atomic_compare_exchange_n(_ptr, _expected, desired, \
        0, success, failure);\
  } else { \
    Lock *l = lock_for_pointer(_ptr);\
    lock(l);\
    if (*_ptr == *_expected) {\
      *_ptr = desired;\
    } else { \
      *_expected = *_ptr;\
      ret = 0; \
    } \
    unlock(l);\
    return ret;\
  } \
}

////////////////////////////////////////////////////////////////////////////////
// Atomic read-modify-write operations for integers of various sizes.
////////////////////////////////////////////////////////////////////////////////

#define __ATOMIC_RMW(n, opname, op) \
_Pragma( TOSTR(redefine_extname __atomic_fetch_##opname##_##n##_c \
			        __atomic_fetch_##opname##_##n) ) \
TYPE##n __atomic_fetch_##opname##_##n##_c (void *ptr, TYPE##n val,\
                                           int model) {\
  TYPE##n tmp; \
  TYPE##n *_ptr = (TYPE##n*)ptr;\
  if (__atomic_always_lock_free(n, 0)) { \
    return __atomic_fetch_##opname##_##n(_ptr, val, model);\
  } else { \
    Lock *l = lock_for_pointer(_ptr);\
    lock(l);\
    tmp = *_ptr;\
    *_ptr op ## = val;\
    unlock(l);\
    return tmp;\
  }\
}

#define __ATOMIC_OPTIMISED_CASES(n) \
__ATOMIC_LOAD_N(n) \
__ATOMIC_STORE_N(n) \
__ATOMIC_EXCHANGE_N(n) \
__ATOMIC_COMPARE_EXCHANGE_N(n) \
__ATOMIC_RMW(n, add, +) \
__ATOMIC_RMW(n, sub, -) \
__ATOMIC_RMW(n, and, &) \
__ATOMIC_RMW(n, or,  |) \
__ATOMIC_RMW(n, xor, ^)

__ATOMIC_OPTIMISED_CASES(1)
__ATOMIC_OPTIMISED_CASES(2)
__ATOMIC_OPTIMISED_CASES(4)
__ATOMIC_OPTIMISED_CASES(8)
#if 0 /* Need a 128bit type */
__ATOMIC_OPTIMISED_CASES(16)
#endif

#endif /* !__STDC_NO_ATOMICS__ */
