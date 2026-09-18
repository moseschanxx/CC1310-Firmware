#include "boot_api.h"
#include <stdint.h>
#include <ti/devices/cc13x0/driverlib/sys_ctrl.h>

#define BL_API_ADDRESS 0x00005000u
#define BL_API_MAGIC 0x424C4150u
#define BL_API_VERSION 1u
#define BL_HANDOFF_ADDRESS 0x20004F00u
#define BL_HANDOFF_MAGIC 0x484E4446u

typedef struct {
    uint32_t magic;
    uint16_t abiVersion;
    uint16_t reserved;
    void (*confirmBoot)(uint32_t bootId);
    void (*requestUpdate)(void);
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

void bl_confirm_boot(void)
{
    const BlApi *table = api();
    volatile BlHandoff *handoff = (volatile BlHandoff *)BL_HANDOFF_ADDRESS;
    if (table && handoff->magic == BL_HANDOFF_MAGIC) {
        table->confirmBoot(handoff->bootId);
        handoff->magic = 0u;
    }
}

void bl_request_update(void)
{
    const BlApi *table = api();
    if (!table) return;
    table->requestUpdate();
    SysCtrlSystemReset();
    for (;;) { }
}

unsigned int bl_get_startup_role(void)
{
    volatile const BlHandoff *handoff = (volatile const BlHandoff *)BL_HANDOFF_ADDRESS;

    /* Standalone J-Link images and legacy/no bootloader deployments default RX. */
    if (handoff->magic != BL_HANDOFF_MAGIC) return BL_APP_ROLE_RX;
    return handoff->appRole == BL_APP_ROLE_TX ? BL_APP_ROLE_TX : BL_APP_ROLE_RX;
}
