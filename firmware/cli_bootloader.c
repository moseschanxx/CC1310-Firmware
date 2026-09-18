#include "cli_bootloader.h"
#include "boot_api.h"
#include <ti/devices/cc13x0/driverlib/sys_ctrl.h>

static void bootloader_command(int argc, char *argv[])
{
    (void)argv;
    if (argc != 1) { cli_error("ARG", "usage: bootloader"); return; }
    if (bl_request_update() != 0) {
        cli_error("BOOT", "metadata_write_failed");
        return;
    }
    cli_ok("rebooting_to_bootloader");
    SysCtrlSystemReset();
    for (;;) { }
}

const CliCommand cli_bootloader_command = {
    "bootloader", "reboot into UART firmware updater", bootloader_command
};
