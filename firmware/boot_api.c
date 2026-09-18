#include "boot_api.h"
#include <stdint.h>

#define BL_API_ADDRESS 0x00005000u
#define BL_API_MAGIC 0x424C4150u
#define BL_API_VERSION 2u
#define BL_HANDOFF_ADDRESS 0x20004F00u
#define BL_HANDOFF_MAGIC 0x484E4446u

typedef struct {
    uint32_t magic;
    uint16_t abiVersion;
    uint16_t reserved;
    int (*confirmBoot)(uint32_t bootId);
    int (*requestUpdate)(void);
} BlApi;

typedef struct {
    uint32_t magic;
    uint32_t bootId;
    uint32_t appRole;
} BlHandoff;

static const BlApi *api(void)
{
    const BlApi *table = (const BlApi *)BL_API_ADDRESS;
    return (table->magic == BL_API_MAGIC && table->abiVersion == BL_API_VERSION) ? table : 0;
}

int bl_confirm_boot(void)
{
    const BlApi *table = api();
    volatile BlHandoff *handoff = (volatile BlHandoff *)BL_HANDOFF_ADDRESS;
    if (table && handoff->magic == BL_HANDOFF_MAGIC) {
        if (table->confirmBoot(handoff->bootId) == 0) {
            handoff->magic = 0u;
            return 0;
        }
    }
    return -1;
}

int bl_request_update(void)
{
    const BlApi *table = api();
    return table ? table->requestUpdate() : -1;
}

unsigned int bl_get_startup_role(void)
{
    volatile const BlHandoff *handoff = (volatile const BlHandoff *)BL_HANDOFF_ADDRESS;

    /* Standalone J-Link images and legacy/no bootloader deployments default RX. */
    if (handoff->magic != BL_HANDOFF_MAGIC) return BL_APP_ROLE_RX;
    return handoff->appRole == BL_APP_ROLE_TX ? BL_APP_ROLE_TX : BL_APP_ROLE_RX;
}
