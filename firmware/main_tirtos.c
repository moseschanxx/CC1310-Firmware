/*
 * Copyright (c) 2016-2017, Texas Instruments Incorporated
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
 *  ======== main_tirtos.c ========
 */
#include <stdint.h>

/* POSIX Header files */
#include <pthread.h>

/* RTOS header files */
#include <ti/sysbios/BIOS.h>

/* Example/Board Header files */
#include "Board.h"
#include "cli_task_cc1310.h"
#include "rf_packet_queue.h"
#include "firmware_mode.h"
#include "firmware_tx.h"

extern void *firmware_rx_thread(void *arg0);

/* Stack size in bytes */
#define THREADSTACKSIZE    1024

/* The boot image uses a compact, explicitly bounded primary heap.  Give the
 * first application task a fixed stack so pthread_create cannot consume or
 * corrupt heap storage while the relocated runtime is being brought up. */
#pragma DATA_ALIGN(mainThreadStack, 8)
static uint8_t mainThreadStack[THREADSTACKSIZE];

/*
 *  ======== main ========
 */
int main(void)
{
    pthread_t           thread;
    pthread_attr_t      attrs;
    struct sched_param  priParam;
    int                 retc;
    int                 detachState;
    FirmwareRole        role;

    /* Call driver init functions */
    Board_initGeneral();
    role = firmware_role_from_metadata();
    if (role == FIRMWARE_ROLE_RX) {
        rf_packet_queue_init();
    }

    /* Set priority and stack size attributes */
    pthread_attr_init(&attrs);
    /* RF processing takes priority over the interactive, low-priority CLI. */
    priParam.sched_priority = 2;

    detachState = PTHREAD_CREATE_DETACHED;
    retc = pthread_attr_setdetachstate(&attrs, detachState);
    if (retc != 0) {
        /* pthread_attr_setdetachstate() failed */
        while (1);
    }

    pthread_attr_setschedparam(&attrs, &priParam);

    retc |= pthread_attr_setstacksize(&attrs, THREADSTACKSIZE);
    if (retc != 0) {
        /* pthread_attr_setstacksize() failed */
        while (1);
    }

    retc = pthread_attr_setstack(&attrs, mainThreadStack, sizeof(mainThreadStack));
    if (retc != 0) {
        /* pthread_attr_setstack() failed */
        while (1);
    }

    retc = pthread_create(&thread, &attrs,
                          role == FIRMWARE_ROLE_TX ? firmware_tx_thread : firmware_rx_thread,
                          NULL);
    if (retc != 0) {
        /* pthread_create() failed */
        while (1);
    }
    if (cli_cc1310_start(role) != 0) {
        /* CLI task creation failed */
        while (1);
    }
    if (role == FIRMWARE_ROLE_RX && rf_packet_print_start() != 0) {
        /* RX queue consumer task creation failed */
        while (1);
    }
    BIOS_start();

    return (0);
}
