#ifndef CLI_PORT_H
#define CLI_PORT_H

#include <stddef.h>
/* Implement this function in the target-specific port. */
void cli_port_write(const char *data, size_t length);

#endif
