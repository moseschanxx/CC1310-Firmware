#include "rf_packet_queue.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Mailbox.h>
#include <ti/drivers/rf/RF.h>

#include "cli_task_cc1310.h"

#define RF_PACKET_QUEUE_DEPTH       32U
#define RF_PACKET_PRINT_STACK_SIZE  2048U
#define RF_PACKET_PRINT_STATS_ENABLE 0U  /* Set to 1U to emit RFSTAT once per second. */
/* Clock_tickPeriod is expressed in microseconds. */
#define RF_PACKET_STATS_PERIOD_TICKS (1000000U / Clock_tickPeriod)
typedef enum {
    RF_PACKET_SOURCE_RADIO
} RfPacketSource;

typedef struct {
    uint32_t sequence;
    uint32_t timestampTicks;
    uint8_t source;
    uint8_t length;
    uint8_t payload[RF_PACKET_QUEUE_PAYLOAD_MAX];
} RfPacketMessage;

/* Mailbox storage includes its queue link before each application message. */
typedef struct {
    Mailbox_MbxElem mailboxElement;
    RfPacketMessage message;
} RfPacketMailboxElement;

static Mailbox_Struct packetMailboxStruct;
static Mailbox_Handle packetMailbox;
static RfPacketMailboxElement packetMailboxBuffer[RF_PACKET_QUEUE_DEPTH];
static uint32_t nextSequence;
static volatile uint8_t packetDumpEnabled;
static volatile RfPacketStats packetStats = {
    .rssiLast = -128,
    .rssiMin = 127,
    .rssiMax = -128
};

static void post_packet(RfPacketSource source, const uint8_t *payload, uint8_t length,
                        uint32_t timestampTicks)
{
    RfPacketMessage message;

    if (packetMailbox == NULL || payload == NULL) return;
    if (length > RF_PACKET_QUEUE_PAYLOAD_MAX) length = RF_PACKET_QUEUE_PAYLOAD_MAX;

    message.sequence = nextSequence++;
    message.timestampTicks = timestampTicks;
    message.source = (uint8_t)source;
    message.length = length;
    memcpy(message.payload, payload, length);
    if (source == RF_PACKET_SOURCE_RADIO) {
        ++packetStats.radioReceived;
    }

    if (Mailbox_post(packetMailbox, &message, BIOS_NO_WAIT)) {
        if (source == RF_PACKET_SOURCE_RADIO) {
            ++packetStats.radioQueued;
        }
    } else if (source == RF_PACKET_SOURCE_RADIO) {
        ++packetStats.appQueueDropped;
    }
}

void rf_packet_queue_init(void)
{
    Mailbox_Params params;

    Mailbox_Params_init(&params);
    params.buf = packetMailboxBuffer;
    params.bufSize = sizeof(packetMailboxBuffer);
    Mailbox_construct(&packetMailboxStruct, sizeof(RfPacketMessage),
                      RF_PACKET_QUEUE_DEPTH, &params, NULL);
    packetMailbox = Mailbox_handle(&packetMailboxStruct);
}

void rf_packet_queue_post_radio(const uint8_t *payload, uint8_t length,
                                uint32_t timestampTicks)
{
    post_packet(RF_PACKET_SOURCE_RADIO, payload, length, timestampTicks);
}

void rf_packet_queue_note_rx_buf_full(void)
{
    ++packetStats.rxBufferFull;
}

void rf_packet_queue_note_rssi(int8_t rssi)
{
    if (rssi == RF_GET_RSSI_ERROR_VAL) return;

    packetStats.rssiLast = rssi;
    if (rssi < packetStats.rssiMin) packetStats.rssiMin = rssi;
    if (rssi > packetStats.rssiMax) packetStats.rssiMax = rssi;
    ++packetStats.rssiSamples;
}

void rf_packet_queue_note_crc_error(int8_t rssi)
{
    ++packetStats.crcErrors;
    rf_packet_queue_note_rssi(rssi);
}

void rf_packet_queue_note_collision(void)
{
    ++packetStats.collisions;
}

void rf_packet_queue_get_stats(RfPacketStats *stats)
{
    if (stats != NULL) {
        *stats = packetStats;
    }
}

void rf_packet_dump_set(uint8_t enabled)
{
    packetDumpEnabled = enabled ? 1U : 0U;
}

uint8_t rf_packet_dump_get(void)
{
    return packetDumpEnabled;
}

static void print_stats(void)
{
    char line[160];
    RfPacketStats stats;
    int length;

    rf_packet_queue_get_stats(&stats);
    length = snprintf(line, sizeof(line),
                      "RFSTAT rx=%lu enq=%lu appdrop=%lu rxfu=%lu crc=%lu coll=%lu rssi=%d[%d,%d] n=%lu\r\n",
                      (unsigned long)stats.radioReceived,
                      (unsigned long)stats.radioQueued,
                      (unsigned long)stats.appQueueDropped,
                      (unsigned long)stats.rxBufferFull,
                      (unsigned long)stats.crcErrors,
                      (unsigned long)stats.collisions,
                      (int)stats.rssiLast,
                      (int)stats.rssiMin,
                      (int)stats.rssiMax,
                      (unsigned long)stats.rssiSamples);
    if (length > 0) {
        if ((size_t)length >= sizeof(line)) length = sizeof(line) - 1;
        cli_cc1310_uart_write(line, (size_t)length);
    }
}

static void print_packet(const RfPacketMessage *message)
{
    char line[160];
    size_t offset;
    uint8_t i;

    offset = (size_t)snprintf(line, sizeof(line), "RX seq=%lu tick=%lu len=%u data=",
                              (unsigned long)message->sequence,
                              (unsigned long)message->timestampTicks,
                              (unsigned int)message->length);
    for (i = 0; i < message->length && offset + 2U < sizeof(line); ++i) {
        offset += (size_t)snprintf(&line[offset], sizeof(line) - offset,
                                   "%02X", message->payload[i]);
    }
    if (offset + 2U < sizeof(line)) {
        line[offset++] = '\r';
        line[offset++] = '\n';
    }
    cli_cc1310_uart_write(line, offset);
}

void *rf_packet_print_thread(void *arg0)
{
    RfPacketMessage message;
    uint32_t lastStatsTick = Clock_getTicks();
    (void)arg0;

    for (;;) {
        if (Mailbox_pend(packetMailbox, &message, RF_PACKET_STATS_PERIOD_TICKS) &&
            rf_packet_dump_get()) {
            print_packet(&message);
        }
        if (RF_PACKET_PRINT_STATS_ENABLE &&
            (uint32_t)(Clock_getTicks() - lastStatsTick) >= RF_PACKET_STATS_PERIOD_TICKS) {
            print_stats();
            lastStatsTick = Clock_getTicks();
        }
    }
}

int rf_packet_print_start(void)
{
    pthread_t thread;
    pthread_attr_t attrs;
    struct sched_param priority;
    int status;

    pthread_attr_init(&attrs);
    priority.sched_priority = 1;
    status = pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    status |= pthread_attr_setschedparam(&attrs, &priority);
    status |= pthread_attr_setstacksize(&attrs, RF_PACKET_PRINT_STACK_SIZE);
    if (status != 0) return -1;
    return pthread_create(&thread, &attrs, rf_packet_print_thread, NULL);
}
