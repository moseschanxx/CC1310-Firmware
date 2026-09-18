#include "firmware_startup.h"

#include <xdc/std.h>
#include <ti/sysbios/hal/Hwi.h>

#include "boot_api.h"

static uint8_t rfReady;
static uint8_t cliReady;
static uint8_t bootConfirmationAttempted;

static void updateReadiness(uint8_t isRfReady)
{
    UInt key;
    uint8_t confirmBoot = 0U;

    key = Hwi_disable();
    if (isRfReady) {
        rfReady = 1U;
    } else {
        cliReady = 1U;
    }
    if (rfReady && cliReady && !bootConfirmationAttempted) {
        bootConfirmationAttempted = 1U;
        confirmBoot = 1U;
    }
    Hwi_restore(key);

    if (confirmBoot) {
        /* A failure deliberately leaves the boot attempt unconfirmed. */
        (void)bl_confirm_boot();
    }
}

void firmwareStartupMarkRfReady(void)
{
    updateReadiness(1U);
}

void firmwareStartupMarkCliReady(void)
{
    updateReadiness(0U);
}
