#ifndef FIRMWARE_STARTUP_H
#define FIRMWARE_STARTUP_H

/* Mark the two operational startup dependencies.  The bootloader receives its
 * confirmation exactly once, only after both RF and UART CLI are ready. */
void firmwareStartupMarkRfReady(void);
void firmwareStartupMarkCliReady(void);

#endif
