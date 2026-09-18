#ifndef RX_BOOT_API_H
#define RX_BOOT_API_H

/* Startup role values written by the bootloader into handoff RAM. */
#define BL_APP_ROLE_UNSET 0u
#define BL_APP_ROLE_RX    1u
#define BL_APP_ROLE_TX    2u

/* ABI exported by the custom bootloader at fixed flash address 0x5000. */
int bl_confirm_boot(void);
int bl_request_update(void);
unsigned int bl_get_startup_role(void);

#endif
