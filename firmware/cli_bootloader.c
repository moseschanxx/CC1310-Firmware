#include "cli_bootloader.h"
#include "boot_api.h"

static void bootloader_command(int argc, char *argv[])
{
    (void)argv;
    if (argc != 1) { cli_error("ARG", "usage: bootloader"); return; }
    cli_ok("rebooting_to_bootloader");
    bl_request_update();
}

const CliCommand cli_bootloader_command = {
    "bootloader", "reboot into UART firmware updater", bootloader_command
};
