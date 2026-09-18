#include "firmware_mode.h"
#include "boot_api.h"

FirmwareRole firmware_role_from_metadata(void)
{
    return bl_get_startup_role() == BL_APP_ROLE_TX ? FIRMWARE_ROLE_TX : FIRMWARE_ROLE_RX;
}

const char *firmware_role_name(FirmwareRole role)
{
    return role == FIRMWARE_ROLE_TX ? "tx" : "rx";
}
