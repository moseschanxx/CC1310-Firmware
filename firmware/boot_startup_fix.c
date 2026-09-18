/*
 * TI-RTOS ROM Hwi_initNVIC assumes reset vectors start at flash address zero.
 * This application starts at 0x8000, so the boot-image build calls this small
 * replacement before SYS/BIOS initializes drivers and tasks.
 */
#include <stdint.h>

#include <ti/devices/cc13x0/inc/hw_nvic.h>
#include <ti/devices/cc13x0/inc/hw_types.h>

#define BOOT_APPLICATION_VECTOR_TABLE 0x00008000u
#define RAM_VECTOR_TABLE              0x20000000u
#define BOOT_DEBUG_RAM_ADDRESS         0x20004E00u
#define SYSTEM_VECTOR_COUNT           15u
#define TOTAL_VECTOR_COUNT            55u

extern void xdc_runtime_Startup_reset__I(void);
extern void _c_int00(void);
extern uint32_t ti_sysbios_heaps_HeapMem_Object__table__V[];
extern uint8_t __primary_heap_start__;
extern uint8_t __primary_heap_end__;
void bootAppInitializeInterrupts(void);
extern int xdc_runtime_System_Module_startup__E(void);
extern int ti_sysbios_knl_Clock_Module_startup__E(int state);
extern int ti_sysbios_knl_Mailbox_Module_startup__E(int state);
extern int ti_sysbios_knl_Swi_Module_startup__E(int state);
extern int ti_sysbios_knl_Task_Module_startup__E(int state);
extern void xdc_runtime_Startup_startMods__I(int *state, int count);

/*
 * Diagnostic reset wrapper.  It makes the direct bootloader handoff and the
 * generated CC1310 reset hook separately observable during integration test.
 */
void ResetISR(void)
{
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x10u;
    /* Do this before any TI-RTOS startup code can service a pending system
     * exception left by the bootloader. */
    bootAppInitializeInterrupts();
    xdc_runtime_Startup_reset__I();
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x11u;
    _c_int00();

    for (;;) {
    }
}

void bootAppInitializeHeap(void)
{
    uintptr_t heapStart = (uintptr_t)&__primary_heap_start__;
    uintptr_t heapEnd = (uintptr_t)&__primary_heap_end__;
    uint32_t *heapObject = ti_sysbios_heaps_HeapMem_Object__table__V;
    uintptr_t alignedStart;
    uint32_t alignment;
    uint32_t blockAlignment;
    uint32_t heapSize;

    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x20u;

    /* HeapMem_initPrimary in the ROM library calls a ROM object accessor
     * before the relocated image's vector state is established.  RX has one
     * statically configured primary heap; initialize that object using the
     * same layout and algorithm as TI's HeapMem.c implementation. */
    alignment = heapObject[1];
    alignedStart = (heapStart + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    blockAlignment = heapObject[5];
    heapSize = (uint32_t)(heapEnd - alignedStart);
    heapSize = (heapSize / blockAlignment) * blockAlignment;

    heapObject[2] = (uint32_t)alignedStart; /* obj->buf */
    heapObject[3] = (uint32_t)alignedStart; /* obj->head.next */
    heapObject[4] = heapSize;               /* obj->head.size */
    ((uint32_t *)alignedStart)[0] = 0u;     /* first free block: next */
    ((uint32_t *)alignedStart)[1] = heapSize; /* first free block: size */
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x21u;
}

int bootAppStartupSystem(void)
{
    int status;

    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x40u;
    status = xdc_runtime_System_Module_startup__E();
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x41u;
    return status;
}

int bootAppStartupClock(int state)
{
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x42u;
    /* The ROM TimerProxy startup is not relocatable.  The timer remains
     * stopped until BIOS_start configures it after the application is alive. */
    (void)state;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x43u;
    return -1; /* xdc.runtime.Startup_DONE */
}

int bootAppStartupMailbox(int state)
{
    int status;

    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x44u;
    status = ti_sysbios_knl_Mailbox_Module_startup__E(state);
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x45u;
    return status;
}

int bootAppStartupSwi(int state)
{
    int status;

    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x46u;
    status = ti_sysbios_knl_Swi_Module_startup__E(state);
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x47u;
    return status;
}

int bootAppStartupTask(int state)
{
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x48u;
    (void)state;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x49u;
    return -1; /* no statically-created application tasks require post-init */
}

int bootAppStartupHwi(int state)
{
    (void)state;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x4Bu;
    return -1;
}

int bootAppStartupTimer(int state)
{
    (void)state;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x4Du;
    return -1;
}

int bootAppStartupTimestamp(int state)
{
    (void)state;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x4Fu;
    return -1;
}

int bootAppStartupHalHwi(int state)
{
    (void)state;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x51u;
    return -1;
}

void bootAppStartupDispatcher(void)
{
    int state[9];

    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x38u;
    xdc_runtime_Startup_startMods__I(state, 9);
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x39u;
}

void bootAppInitializeInterrupts(void)
{
    const uint32_t *flashVectors = (const uint32_t *)BOOT_APPLICATION_VECTOR_TABLE;
    volatile uint32_t *ramVectors = (volatile uint32_t *)RAM_VECTOR_TABLE;
    uint32_t vectorIndex;

    /* The bootloader can leave peripheral sources asserted or enabled.  The
     * relocated application has not installed its driver HWIs yet, so mask
     * and unpend every external interrupt before replacing VTOR.  Individual
     * drivers (RF, UART, I2C, ...) enable their own IRQ only after plugging
     * the corresponding handler. */
    HWREG(NVIC_DIS0) = NVIC_DIS0_INT_M;
    HWREG(NVIC_DIS1) = NVIC_DIS1_INT_M;
    HWREG(NVIC_UNPEND0) = NVIC_UNPEND0_INT_M;
    HWREG(NVIC_UNPEND1) = NVIC_UNPEND1_INT_M;

    /* Copy app exceptions, not the bootloader's exceptions at flash address 0. */
    for (vectorIndex = 0; vectorIndex < SYSTEM_VECTOR_COUNT; vectorIndex++) {
        ramVectors[vectorIndex] = flashVectors[vectorIndex];
    }
    /* Driver HWIs are plugged before they are enabled.  Until then, route any
     * unexpected peripheral interrupt to the app's fault handler. */
    for (; vectorIndex < TOTAL_VECTOR_COUNT; vectorIndex++) {
        ramVectors[vectorIndex] = flashVectors[3];
    }
    HWREG(NVIC_VTABLE) = RAM_VECTOR_TABLE;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 0x30u;
}
