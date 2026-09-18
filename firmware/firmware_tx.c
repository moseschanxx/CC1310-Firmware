/*
 * Copyright (c) 2019, Texas Instruments Incorporated
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

/***** Includes *****/
/* Standard C Libraries */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* TI Drivers */
#include <ti/drivers/rf/RF.h>
#include <ti/drivers/PIN.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>

/* Driverlib Header files */
#include DeviceFamily_constructPath(driverlib/rf_prop_mailbox.h)

/* Board Header files */
#include "Board.h"
#include "smartrf_settings/smartrf_settings.h"
#include "boot_api.h"
#include "cli_core.h"
#include "cli_task_cc1310.h"
#include "firmware_tx.h"

/***** Defines *****/

/* Packet TX Configuration */
#define PAYLOAD_LENGTH      30
#define CONTINUOUS_TX_TEST_MODE  0  /* 1: continuous PRBS-15 RF test signal */

/* Synchronization packet format. All multi-byte values use big-endian order.
 * Keep deployment-specific identifiers here instead of scattering them across
 * command parsing and RF transmit code. */
#define SYNC_PACKET_MAGIC           0x5359U /* ASCII "SY" */
#define SYNC_PACKET_TYPE_TIME       0x01U
#define SYNC_PACKET_TYPE_FRAME      0x02U
#define SYNC_PACKET_SITE_ID         0x0000U
#define SYNC_PACKET_SESSION_ID      0x00U
#define SYNC_PACKET_HEADER_LENGTH   8U
#define SYNC_PACKET_CRC_LENGTH      2U
#define SYNC_TIME_PAYLOAD_LENGTH    8U
#define SYNC_FRAME_PAYLOAD_LENGTH   4U
#define TX_CLI_LINE_LENGTH          64U

/* Enable a DIO1 pulse around RF transmission for latency measurements. */
#ifndef RF_PACKET_TX_DIO1_TIMING_ENABLE
#define RF_PACKET_TX_DIO1_TIMING_ENABLE 1
#endif

/***** Prototypes *****/

static RF_Object rfObject;
static RF_Handle rfHandle;
/* The TX radio owner remains visible to diagnostics after initialization. */
static Semaphore_Struct txIdleSemaphore;

#if RF_PACKET_TX_DIO1_TIMING_ENABLE
/* Pin driver state for the TX latency measurement signal. */
static PIN_Handle dio1PinHandle;
static PIN_State dio1PinState;
#endif

static uint8_t packet[PAYLOAD_LENGTH];
#if !CONTINUOUS_TX_TEST_MODE
static uint8_t syncSequence;
static uint32_t randomState = 0x4A3B2C1DU;
#endif

#if CONTINUOUS_TX_TEST_MODE
/* Kept static because the RF core uses this command for the test duration. */
static rfc_CMD_TX_TEST_t continuousTxTestCmd;
#endif

#if RF_PACKET_TX_DIO1_TIMING_ENABLE
/* DIO1 idles low and is pulsed high while a TX command executes. */
static PIN_Config dio1PinTable[] =
{
    Board_DIO1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};
#endif

/***** Function definitions *****/

#if !CONTINUOUS_TX_TEST_MODE
static uint16_t calculateCrc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFFU;
    uint8_t bit;

    while (length-- != 0U) {
        crc ^= (uint16_t)(*data++) << 8;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U ? (uint16_t)((crc << 1) ^ 0x1021U) :
                                           (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t nextRandomByte(void)
{
    /* Reserved bytes have no protocol meaning; a small PRNG is sufficient. */
    randomState = randomState * 1664525U + 1013904223U + Clock_getTicks();
    return (uint8_t)(randomState >> 24);
}

static void writeUint32Be(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void writeUint64Be(uint8_t *destination, uint64_t value)
{
    uint8_t index;

    for (index = 0U; index < SYNC_TIME_PAYLOAD_LENGTH; ++index) {
        destination[index] = (uint8_t)(value >> ((7U - index) * 8U));
    }
}

static int parseHexUint64(const char *text, uint8_t maximumDigits, uint64_t *value)
{
    uint64_t result = 0U;
    uint8_t digits = 0U;
    uint8_t digit;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    while (*text != '\0') {
        if (*text >= '0' && *text <= '9') digit = (uint8_t)(*text - '0');
        else if (*text >= 'a' && *text <= 'f') digit = (uint8_t)(*text - 'a' + 10U);
        else if (*text >= 'A' && *text <= 'F') digit = (uint8_t)(*text - 'A' + 10U);
        else return -1;
        if (digits++ == maximumDigits) return -1;
        result = (result << 4) | digit;
        ++text;
    }
    if (digits == 0U) return -1;
    *value = result;
    return 0;
}

static void buildSyncPacket(uint8_t type, const uint8_t *payload, uint8_t payloadLength)
{
    uint8_t index;
    uint16_t crc;

    packet[0] = (uint8_t)(SYNC_PACKET_MAGIC >> 8);
    packet[1] = (uint8_t)SYNC_PACKET_MAGIC;
    packet[2] = type;
    packet[3] = payloadLength;
    packet[4] = (uint8_t)(SYNC_PACKET_SITE_ID >> 8);
    packet[5] = (uint8_t)SYNC_PACKET_SITE_ID;
    packet[6] = syncSequence++;
    packet[7] = SYNC_PACKET_SESSION_ID;
    memcpy(&packet[SYNC_PACKET_HEADER_LENGTH], payload, payloadLength);
    for (index = (uint8_t)(SYNC_PACKET_HEADER_LENGTH + payloadLength);
         index < (PAYLOAD_LENGTH - SYNC_PACKET_CRC_LENGTH); ++index) {
        packet[index] = nextRandomByte();
    }
    crc = calculateCrc16(packet, PAYLOAD_LENGTH - SYNC_PACKET_CRC_LENGTH);
    packet[PAYLOAD_LENGTH - 2U] = (uint8_t)(crc >> 8);
    packet[PAYLOAD_LENGTH - 1U] = (uint8_t)crc;
}

static void writeCli(const char *message)
{
    cli_cc1310_uart_write(message, strlen(message));
}

static void processSyncCommand(char *line)
{
    char *command = strtok(line, " \t");
    char *argument = strtok(NULL, " \t");
    char *extraArgument = strtok(NULL, " \t");
    uint8_t payload[SYNC_TIME_PAYLOAD_LENGTH];
    uint64_t value;
    RF_EventMask reason;
    uint32_t status;
    char response[64];

    if (command == NULL) return;
    if (argument == NULL || extraArgument != NULL) {
        writeCli("ERR ARG usage: tx sync_time <utc_hex> | tx sync_frame <exposure_hex>\r\n");
        return;
    }

    if (strcmp(command, "sync_time") == 0) {
        if (parseHexUint64(argument, 16U, &value) != 0) {
            writeCli("ERR ARG utc_hex_must_be_uint64\r\n");
            return;
        }
        writeUint64Be(payload, value);
        buildSyncPacket(SYNC_PACKET_TYPE_TIME, payload, SYNC_TIME_PAYLOAD_LENGTH);
    } else if (strcmp(command, "sync_frame") == 0) {
        if (parseHexUint64(argument, 8U, &value) != 0) {
            writeCli("ERR ARG exposure_hex_must_be_uint32\r\n");
            return;
        }
        writeUint32Be(payload, (uint32_t)value);
        buildSyncPacket(SYNC_PACKET_TYPE_FRAME, payload, SYNC_FRAME_PAYLOAD_LENGTH);
    } else {
        writeCli("ERR CMD unknown_command; use help\r\n");
        return;
    }

#if RF_PACKET_TX_DIO1_TIMING_ENABLE
    PIN_setOutputValue(dio1PinHandle, Board_DIO1, 1);
#endif
    reason = RF_runCmd(rfHandle, (RF_Op*)&RF_cmdPropTx, RF_PriorityNormal, NULL, 0);
#if RF_PACKET_TX_DIO1_TIMING_ENABLE
    PIN_setOutputValue(dio1PinHandle, Board_DIO1, 0);
#endif
    status = ((volatile RF_Op*)&RF_cmdPropTx)->status;
    if (reason == RF_EventLastCmdDone && status == PROP_DONE_OK) {
        snprintf(response, sizeof(response), "OK %s seq=%u\r\n", command,
                 (unsigned int)packet[6]);
    } else {
        snprintf(response, sizeof(response), "ERR RF event=0x%08lx status=0x%08lx\r\n",
                 (unsigned long)reason, (unsigned long)status);
    }
    writeCli(response);
}

static void tx_command(int argc, char *argv[])
{
    char line[TX_CLI_LINE_LENGTH];

    if (argc < 2 || argc > 3) {
        cli_error("ARG", "usage: tx sync_time <utc_hex> | tx sync_frame <exposure_hex>");
        return;
    }
    if (argc == 2) {
        snprintf(line, sizeof(line), "%s", argv[1]);
    } else {
        snprintf(line, sizeof(line), "%s %s", argv[1], argv[2]);
    }
    processSyncCommand(line);
}

const CliCommand cli_tx_command = {
    "tx", "tx sync_time <utc_hex> | tx sync_frame <exposure_hex>", tx_command
};
#endif /* !CONTINUOUS_TX_TEST_MODE */

/* Initialize the TX radio. The common CLI task owns UART0. */
void *firmware_tx_thread(void *arg0)
{
    RF_Params rfParams;
    RF_Params_init(&rfParams);

#if RF_PACKET_TX_DIO1_TIMING_ENABLE
    /* Open DIO1 for TX latency measurements; idle level is low. */
    dio1PinHandle = PIN_open(&dio1PinState, dio1PinTable);
    if (dio1PinHandle == NULL)
    {
        while(1);
    }
#endif

    /* In variable-length mode, pktLen supplies the RF length byte. */
    RF_cmdPropTx.pktLen = PAYLOAD_LENGTH;
    RF_cmdPropTx.pPkt = packet;
    RF_cmdPropTx.startTrigger.triggerType = TRIG_NOW;

    /* Request access to the radio */
#if defined(DeviceFamily_CC26X0R2)
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioSetup, &rfParams);
#else
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup, &rfParams);
#endif// DeviceFamily_CC26X0R2

    /* Tune synchronously before accepting CLI commands. Keeping the radio
     * ready avoids a yield/power-down race when a second command arrives. */
    if (RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs, RF_PriorityNormal, NULL, 0) !=
        RF_EventLastCmdDone) {
        while (1);
    }

    /* The selected TX role has initialized UART and the RF synthesizer. */
    bl_confirm_boot();

#if CONTINUOUS_TX_TEST_MODE
    /*
     * Continuously transmit a PRBS-15 modulated signal at the configured
     * frequency.  This command intentionally never completes; reset the
     * device or cancel the RF command to leave test mode.
     */
    continuousTxTestCmd.commandNo = CMD_TX_TEST;
    continuousTxTestCmd.startTrigger.triggerType = TRIG_NOW;
    continuousTxTestCmd.startTrigger.pastTrig = 1;
    continuousTxTestCmd.config.bUseCw = 0;
    continuousTxTestCmd.config.bFsOff = 0;
    continuousTxTestCmd.config.whitenMode = 2; /* PRBS-15 */
    continuousTxTestCmd.txWord = 0xAAAA;
    continuousTxTestCmd.endTrigger.triggerType = TRIG_NEVER;

    RF_runCmd(rfHandle, (RF_Op *)&continuousTxTestCmd,
              RF_PriorityNormal, NULL, 0);

    /* CMD_TX_TEST only returns after it has been externally cancelled. */
    while (1);
#else
    /*
     * The common CLI task owns UART0 and invokes cli_tx_command(), but keep
     * the radio owner Task alive.  Returning from this detached pthread
     * deletes its SYS/BIOS Task, which hid radio_tx from the stack command.
     */
    Semaphore_construct(&txIdleSemaphore, 0, NULL);
    Semaphore_pend(Semaphore_handle(&txIdleSemaphore), BIOS_WAIT_FOREVER);
    return NULL;
#endif /* CONTINUOUS_TX_TEST_MODE */
}
