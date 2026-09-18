#ifndef FIRMWARE_MODE_H
#define FIRMWARE_MODE_H

#include <stdint.h>

typedef enum {
    FIRMWARE_ROLE_UNSET = 0u,
    FIRMWARE_ROLE_RX = 1u,
    FIRMWARE_ROLE_TX = 2u
} FirmwareRole;

/* Only an explicit TX setting selects TX. All other values fail safe to RX. */
FirmwareRole firmware_role_from_metadata(void);
const char *firmware_role_name(FirmwareRole role);

#endif
