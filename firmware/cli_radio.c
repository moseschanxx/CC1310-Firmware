#include "cli_radio.h"

#include <stdio.h>
#include <string.h>

#include "rf_packet_queue.h"

static void radio_usage(void)
{
    cli_error("ARG", "usage: rx status | rx dump on|off");
}

static void radio_status(void)
{
    RfPacketStats stats;
    /* Kept out of the CLI task stack; formatting status previously made the
     * 1 KiB CLI stack too tight.  Commands execute serially in that task. */
    static char response[156];

    rf_packet_queue_get_stats(&stats);
    snprintf(response, sizeof(response),
             "rx=%lu enq=%lu drop=%lu full=%lu crc=%lu coll=%lu rssi=%d[%d,%d] samples=%lu dump=%s",
             (unsigned long)stats.radioReceived,
             (unsigned long)stats.radioQueued,
             (unsigned long)stats.appQueueDropped,
             (unsigned long)stats.rxBufferFull,
             (unsigned long)stats.crcErrors,
             (unsigned long)stats.collisions,
             (int)stats.rssiLast,
             (int)stats.rssiMin,
             (int)stats.rssiMax,
             (unsigned long)stats.rssiSamples,
             rf_packet_dump_get() ? "on" : "off");
    cli_ok(response);
}

static void radio_command(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        radio_status();
    } else if (argc == 3 && strcmp(argv[1], "dump") == 0) {
        if (strcmp(argv[2], "on") == 0) {
            rf_packet_dump_set(1U);
            cli_ok("rx_dump=on");
        } else if (strcmp(argv[2], "off") == 0) {
            rf_packet_dump_set(0U);
            cli_ok("rx_dump=off");
        } else {
            radio_usage();
        }
    } else {
        radio_usage();
    }
}

const CliCommand cli_rx_command = { "rx", "rx status | rx dump on|off", radio_command };
