#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include "firmware_mode.h"

/* DIO16=SCL, DIO17=SDA; 7-bit address 0x2a. */
int i2c_slave_start(FirmwareRole role);
void i2c_slave_cli_command(int argc, char *argv[]);
void *i2c_slave_thread(void *arg0);

#endif
