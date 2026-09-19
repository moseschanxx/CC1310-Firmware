#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include <ti/drivers/PIN.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/devices/cc13x0/inc/hw_ints.h>
#include <ti/devices/cc13x0/driverlib/i2c.h>
#include <ti/devices/cc13x0/driverlib/ioc.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/hal/Hwi.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>

#include "Board.h"
#include "cli_core.h"
#include "firmware_build.h"
#include "i2c_slave.h"

#define I2C_SLAVE_ADDRESS       0x2AU
#define I2C_REGISTER_COUNT      64U
#define I2C_WRITE_MAX           I2C_REGISTER_COUNT
#define I2C_DUMP_MAX            I2C_WRITE_MAX
#define I2C_COMMAND_REGISTER    0x40U
#define I2C_COMMAND_CLEAR_STATS 0x01U
#define I2C_LOOPBACK_REGISTER   0x80U
#define I2C_LOOPBACK_SIZE       64U
#define I2C_WORKER_STACK        1024U

#define I2C_DUMP_WRITE          1U
#define I2C_DUMP_READ           2U

static PIN_State i2cPinState;
static PIN_Handle i2cPinHandle;
static Hwi_Struct i2cHwiStruct;
static Semaphore_Struct workSemaphoreStruct;
static uint8_t registers[2][I2C_REGISTER_COUNT];
static volatile uint8_t activeRegisters;
static volatile uint8_t registerPointer;
static volatile uint8_t receiveCount;
static volatile uint8_t receiveBuffer[I2C_WRITE_MAX];
static volatile uint8_t pendingRegister;
static volatile uint8_t pendingCount;
static volatile uint8_t pendingBuffer[I2C_DUMP_MAX];
static volatile uint8_t pendingDirection;
static volatile uint8_t transmitRegisters;
static volatile uint8_t transmitStartRegister;
static volatile uint8_t transmitRegisterSnapshot;
static volatile uint8_t transmitPointerValid;
static volatile uint8_t transmitLoopback;
static volatile uint8_t transmitLength;
static volatile uint8_t transmitBuffer[I2C_DUMP_MAX];
static volatile uint8_t loopbackBuffer[I2C_LOOPBACK_SIZE];
static volatile uint32_t transactionCount;
static volatile uint32_t errorCount;
static volatile uint8_t dumpEnabled;
static FirmwareRole currentRole;

static int i2cInitializeHardware(void);

static uint8_t isLoopbackWrite(uint8_t startRegister, uint8_t length)
{
    return startRegister >= I2C_LOOPBACK_REGISTER &&
           startRegister < I2C_LOOPBACK_REGISTER + I2C_LOOPBACK_SIZE &&
           length <= I2C_LOOPBACK_SIZE - (startRegister - I2C_LOOPBACK_REGISTER);
}

static char hexDigit(uint8_t value)
{
    value &= 0x0FU;
    return value < 10U ? (char)('0' + value) : (char)('a' + value - 10U);
}

static void writeUint32Le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void publishRegisters(void)
{
    uint8_t inactive = activeRegisters ^ 1U;
    uint32_t key;

    memset(registers[inactive], 0, I2C_REGISTER_COUNT);
    registers[inactive][0] = 'I';
    registers[inactive][1] = '2';
    registers[inactive][2] = 'C';
    registers[inactive][3] = 'S';
    registers[inactive][4] = 1U;
    writeUint32Le(&registers[inactive][5], FIRMWARE_VERSION_CODE);
    registers[inactive][9] = (uint8_t)currentRole;
    writeUint32Le(&registers[inactive][10], transactionCount);
    writeUint32Le(&registers[inactive][14], errorCount);
    writeUint32Le(&registers[inactive][18], Clock_getTicks());
    key = Hwi_disable();
    activeRegisters = inactive;
    Hwi_restore(key);
}

static void i2cInterruptHandler(UArg arg)
{
    uint32_t flags = I2CSlaveIntStatus(I2C0_BASE, true);
    uint32_t status;
    uint8_t value;
    uint8_t transmitOffset;

    (void)arg;
    I2CSlaveIntClear(I2C0_BASE, flags);
    (void)I2CSlaveIntStatus(I2C0_BASE, false);
    if ((flags & I2C_SLAVE_INT_DATA) != 0U) {
        status = I2CSlaveStatus(I2C0_BASE);
        if (status == I2C_SLAVE_ACT_TREQ) {
            if (transmitPointerValid != 0U) {
                transmitOffset = (registerPointer - transmitStartRegister) &
                                 (I2C_REGISTER_COUNT - 1U);
                if (transmitLoopback != 0U) {
                    if (registerPointer >= I2C_LOOPBACK_REGISTER &&
                        registerPointer < I2C_LOOPBACK_REGISTER + I2C_LOOPBACK_SIZE) {
                        value = loopbackBuffer[registerPointer - I2C_LOOPBACK_REGISTER];
                    } else {
                        value = 0U;
                    }
                    ++registerPointer;
                } else {
                    if (transmitRegisters > 1U) {
                        transmitRegisters = transmitRegisterSnapshot;
                    }
                    value = registers[transmitRegisters][registerPointer++ &
                                                         (I2C_REGISTER_COUNT - 1U)];
                }
                transmitBuffer[transmitOffset] = value;
                if (transmitLength <= transmitOffset) {
                    transmitLength = transmitOffset + 1U;
                }
            } else {
                /* I2C scan uses an address(R)+one-byte receive probe without
                 * a preceding register pointer.  ACK it with a fixed byte,
                 * but do not consume the previous register transaction or
                 * create an ipc read dump record. */
                value = 0U;
            }
            I2CSlaveDataPut(I2C0_BASE, value);
        } else if (status == I2C_SLAVE_ACT_RREQ_FBR) {
            registerPointer = (uint8_t)I2CSlaveDataGet(I2C0_BASE);
            receiveCount = 0U;
            /* A register-pointer byte starts a new master transaction.  The
             * following repeated START may be a read, so discard any capture
             * state left by a preceding read before the first TREQ arrives. */
            transmitRegisters = 0xFFU;
            transmitStartRegister = registerPointer;
            transmitRegisterSnapshot = activeRegisters;
            transmitPointerValid = 1U;
            transmitLoopback = registerPointer >= I2C_LOOPBACK_REGISTER &&
                               registerPointer < I2C_LOOPBACK_REGISTER + I2C_LOOPBACK_SIZE;
            transmitLength = 0U;
        } else if (status == I2C_SLAVE_ACT_RREQ) {
            value = (uint8_t)I2CSlaveDataGet(I2C0_BASE);
            if (receiveCount < I2C_WRITE_MAX) {
                receiveBuffer[receiveCount++] = value;
            } else {
                ++errorCount;
            }
        }
    }
    if ((flags & I2C_SLAVE_INT_STOP) != 0U && receiveCount != 0U) {
        uint8_t index;

        if (pendingCount != 0U) {
            ++errorCount;
            receiveCount = 0U;
            transmitRegisters = 0xFFU;
            transmitPointerValid = 0U;
            transmitLoopback = 0U;
            transmitLength = 0U;
            return;
        }
        if (isLoopbackWrite(registerPointer, receiveCount) != 0U) {
            for (index = 0U; index < receiveCount; ++index) {
                loopbackBuffer[registerPointer - I2C_LOOPBACK_REGISTER + index] =
                    receiveBuffer[index];
            }
        }
        pendingRegister = registerPointer;
        pendingCount = receiveCount;
        pendingDirection = I2C_DUMP_WRITE;
        for (index = 0U; index < receiveCount; ++index) {
            pendingBuffer[index] = receiveBuffer[index];
        }
        receiveCount = 0U;
        ++transactionCount;
        Semaphore_post(Semaphore_handle(&workSemaphoreStruct));
    } else if ((flags & I2C_SLAVE_INT_STOP) != 0U &&
               transmitPointerValid != 0U && transmitLength != 0U &&
               dumpEnabled != 0U) {
        uint8_t index;

        if (pendingCount != 0U) {
            ++errorCount;
            transmitRegisters = 0xFFU;
            transmitPointerValid = 0U;
            transmitLoopback = 0U;
            transmitLength = 0U;
            return;
        }
        pendingRegister = transmitStartRegister;
        pendingCount = transmitLength;
        pendingDirection = I2C_DUMP_READ;
        for (index = 0U; index < pendingCount; ++index) {
            pendingBuffer[index] = transmitBuffer[index];
        }
        transmitLength = 0U;
        Semaphore_post(Semaphore_handle(&workSemaphoreStruct));
    }
    if ((flags & I2C_SLAVE_INT_STOP) != 0U) {
        transmitRegisters = 0xFFU;
        transmitPointerValid = 0U;
        transmitLoopback = 0U;
        transmitLength = 0U;
    }
}

void i2c_slave_cli_command(int argc, char *argv[])
{
    if (argc == 3 && strcmp(argv[1], "dump") == 0) {
        if (strcmp(argv[2], "on") == 0) {
            dumpEnabled = 1U;
            cli_ok("ipc_dump=on");
            return;
        }
        if (strcmp(argv[2], "off") == 0) {
            dumpEnabled = 0U;
            cli_ok("ipc_dump=off");
            return;
        }
    }
    cli_error("ARG", "usage: ipc dump on|off");
}

void *i2c_slave_thread(void *arg0)
{
    static char dumpMessage[192];
    uint8_t dumpBuffer[I2C_DUMP_MAX];
    uint8_t dumpCount;
    uint8_t dumpRegister;
    uint8_t dumpDirection;
    uint8_t index;
    size_t length;

    (void)arg0;
    for (;;) {
        (void)Semaphore_pend(Semaphore_handle(&workSemaphoreStruct), 100U);
        dumpCount = pendingCount;
        dumpRegister = pendingRegister;
        dumpDirection = pendingDirection;
        for (index = 0U; index < dumpCount; ++index) {
            dumpBuffer[index] = pendingBuffer[index];
        }
        if (dumpDirection == I2C_DUMP_WRITE &&
            pendingRegister == I2C_COMMAND_REGISTER && pendingCount == 1U &&
            pendingBuffer[0] == I2C_COMMAND_CLEAR_STATS) {
            transactionCount = 0U;
            errorCount = 0U;
        } else if (dumpDirection == I2C_DUMP_WRITE && pendingCount != 0U &&
                   isLoopbackWrite(dumpRegister, dumpCount) == 0U) {
            ++errorCount;
        }
        pendingCount = 0U;
        transmitRegisters = 0xFFU;
        publishRegisters();
        if (dumpEnabled != 0U && dumpCount != 0U) {
            length = 0U;
            if (dumpDirection == I2C_DUMP_READ) {
                memcpy(dumpMessage, "ipc read reg=0x", 15U);
                length = 15U;
            } else {
                memcpy(dumpMessage, "ipc write reg=0x", 16U);
                length = 16U;
            }
            dumpMessage[length++] = hexDigit(dumpRegister >> 4);
            dumpMessage[length++] = hexDigit(dumpRegister);
            memcpy(&dumpMessage[length], " len=", 5U);
            length += 5U;
            if (dumpCount >= 10U) {
                dumpMessage[length++] = (char)('0' + (dumpCount / 10U));
            }
            dumpMessage[length++] = (char)('0' + (dumpCount % 10U));
            memcpy(&dumpMessage[length], " data=", 6U);
            length += 6U;
            for (index = 0U; index < dumpCount; ++index) {
                dumpMessage[length++] = hexDigit(dumpBuffer[index] >> 4);
                dumpMessage[length++] = hexDigit(dumpBuffer[index]);
            }
            dumpMessage[length] = '\0';
            cli_ok(dumpMessage);
        }
    }
}

static int i2cInitializeHardware(void)
{
    PIN_Config pins[] = {
        Board_I2C0_SCL0 | PIN_INPUT_EN | PIN_PULLUP | PIN_OPENDRAIN | PIN_DRVSTR_MIN,
        Board_I2C0_SDA0 | PIN_INPUT_EN | PIN_PULLUP | PIN_OPENDRAIN | PIN_DRVSTR_MIN,
        PIN_TERMINATE
    };
    Hwi_Params hwiParams;

    i2cPinHandle = PIN_open(&i2cPinState, pins);
    if (i2cPinHandle == NULL) return -1;
    PINCC26XX_setMux(i2cPinHandle, Board_I2C0_SDA0, IOC_PORT_MCU_I2C_MSSDA);
    PINCC26XX_setMux(i2cPinHandle, Board_I2C0_SCL0, IOC_PORT_MCU_I2C_MSSCL);
    Power_setDependency(PowerCC26XX_PERIPH_I2C0);
    Power_setConstraint(PowerCC26XX_DISALLOW_STANDBY);
    publishRegisters();
    I2CSlaveInit(I2C0_BASE, I2C_SLAVE_ADDRESS);
    Hwi_Params_init(&hwiParams);
    hwiParams.enableInt = false;
    Hwi_construct(&i2cHwiStruct, INT_I2C_IRQ, i2cInterruptHandler,
                  &hwiParams, NULL);
    I2CSlaveIntClear(I2C0_BASE, I2C_SLAVE_INT_START | I2C_SLAVE_INT_DATA | I2C_SLAVE_INT_STOP);
    I2CSlaveIntEnable(I2C0_BASE, I2C_SLAVE_INT_START | I2C_SLAVE_INT_DATA | I2C_SLAVE_INT_STOP);
    Hwi_enableInterrupt(INT_I2C_IRQ);

    return 0;
}

int i2c_slave_start(FirmwareRole role)
{
    pthread_t thread;
    pthread_attr_t attributes;
    struct sched_param priority;
    int status;

    currentRole = role;
    transmitRegisters = 0xFFU;
    transmitPointerValid = 0U;
    transmitLoopback = 0U;
    Semaphore_construct(&workSemaphoreStruct, 0, NULL);
    /* SYS/BIOS owns the application's vector table.  Do not use driverlib
     * I2CIntRegister(), which creates a second table and changes VTOR. */
    if (i2cInitializeHardware() != 0) return -1;

    pthread_attr_init(&attributes);
    priority.sched_priority = 1;
    status = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    status |= pthread_attr_setschedparam(&attributes, &priority);
    status |= pthread_attr_setstacksize(&attributes, I2C_WORKER_STACK);
    if (status != 0) return -1;
    return pthread_create(&thread, &attributes, i2c_slave_thread, NULL);
}
