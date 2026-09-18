/* Minimal reset wrapper for the relocated, non-ROM SYS/BIOS application. */

extern void _c_int00(void);

/* This runs directly after the bootloader loads the app vector table. */
void ResetISR(void)
{
    _c_int00();

    for (;;) {
    }
}
