#ifndef RF_PACKET_QUEUE_H
#define RF_PACKET_QUEUE_H

#include <stdint.h>

#define RF_PACKET_QUEUE_PAYLOAD_MAX 30U

typedef struct {
    uint32_t radioReceived;
    uint32_t radioQueued;
    uint32_t appQueueDropped;
    uint32_t rxBufferFull;
    uint32_t crcErrors;
    uint32_t collisions;
    uint32_t rssiSamples;
    int8_t rssiLast;
    int8_t rssiMin;
    int8_t rssiMax;
} RfPacketStats;

void rf_packet_queue_init(void);
int rf_packet_print_start(void);
void *rf_packet_print_thread(void *arg0);
void rf_packet_queue_post_radio(const uint8_t *payload, uint8_t length,
                                uint32_t timestampTicks);
void rf_packet_queue_note_rx_buf_full(void);
void rf_packet_queue_note_crc_error(int8_t rssi);
void rf_packet_queue_note_collision(void);
void rf_packet_queue_note_rssi(int8_t rssi);
void rf_packet_queue_get_stats(RfPacketStats *stats);
void rf_packet_dump_set(uint8_t enabled);
uint8_t rf_packet_dump_get(void);

#endif
