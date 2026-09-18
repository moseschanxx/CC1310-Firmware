#ifndef FIRMWARE_TX_H
#define FIRMWARE_TX_H

#include "cli_core.h"

void *firmware_tx_thread(void *arg0);
extern const CliCommand cli_tx_command;

#endif
