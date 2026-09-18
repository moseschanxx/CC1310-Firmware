/****************************************************************************/
/*  ATOMIC_FLAG.C                                                           */
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
#ifndef __STDC_NO_ATOMICS__

#include <_ti_config.h>
#include <stdatomic.h>

#undef atomic_flag_test_and_set_explicit
#undef atomic_flag_test_and_set
#undef atomic_flag_clear_explicit
#undef atomic_flag_clear

/*---------------------------------------------------------------------------
 * C11 7.7.18 Atomic flag operations.
 *---------------------------------------------------------------------------*/

/*****************************************************************************
 *
 *  ATOMIC_FLAG_TEST_AND_SET - Atomically sets the value pointed to by 
 *  object to true. Memory is affected according to the value of order.
 *  Return the value of the object immediately before the effects.
 *
 *****************************************************************************/
_CODE_ACCESS _Bool 
atomic_flag_test_and_set_explicit(volatile atomic_flag *object, 
                                  memory_order order)
{
   return atomic_exchange_explicit(&object->flag, 1, order);
}

_CODE_ACCESS _Bool atomic_flag_test_and_set(volatile atomic_flag *object)
{
   return atomic_exchange_explicit(&object->flag, 1, __ATOMIC_SEQ_CST);
}

/*****************************************************************************
 *
 *  ATOMIC_FLAG_CLEAR - The order argument shall not be memory_order_acquire 
 *  nor memory_order_acq_rel. Atomically sets the value pointed to by 
 *  object to false.  Memory is affected according to the value of order.
 *
 *****************************************************************************/
_CODE_ACCESS void atomic_flag_clear_explicit(volatile atomic_flag *object, 
                                             memory_order order)
{
   return atomic_store_explicit(&object->flag, 0, order);
}

_CODE_ACCESS void atomic_flag_clear(volatile atomic_flag *object)
{
   return atomic_store_explicit(&object->flag, 0, __ATOMIC_SEQ_CST);
}

#endif /* !__STDC_NO_ATOMICS__ */
