#ifndef CLI_TASK_CC1310_H
#define CLI_TASK_CC1310_H

#include <stddef.h>
#include "firmware_mode.h"

int cli_cc1310_start(FirmwareRole role);
void cli_cc1310_uart_write(const char *data, size_t length);

#endif
