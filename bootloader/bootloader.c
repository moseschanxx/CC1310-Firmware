/* Minimal UART firmware-update bootloader for the CC1310 combined firmware. */
#include "Board.h"
#include <stdint.h>
#include <string.h>
#include <ti/devices/cc13x0/driverlib/cpu.h>
#include <ti/devices/cc13x0/driverlib/flash.h>
#include <ti/devices/cc13x0/driverlib/sys_ctrl.h>
#include <ti/devices/cc13x0/inc/hw_nvic.h>
#include <ti/devices/cc13x0/inc/hw_types.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/UART.h>

/* The bootloader owns 0x0000..0x7fff; the application begins at 0x8000. */
#define APPLICATION_FLASH_START 0x00008000u
#define APPLICATION_FLASH_END 0x0001F000u
#define METADATA_PAGE_PRIMARY 0x00006000u
#define METADATA_PAGE_SECONDARY 0x00007000u
#define FLASH_PAGE_SIZE 0x00001000u
#define APPLICATION_RAM_START 0x20000000u
#define APPLICATION_RAM_END 0x20005000u
#define BOOT_HANDOFF_RAM_ADDRESS 0x20004F00u
#define BOOT_DEBUG_RAM_ADDRESS 0x20004E00u
#define METADATA_MAGIC 0x424C4D44u
#define PACKAGE_MAGIC 0x4B505746u
#define FIRMWARE_TARGET_ID 0x4343314Du /* "CC1M" */
#define BOOT_HANDOFF_MAGIC 0x484E4446u
#define BOOT_API_MAGIC 0x424C4150u
#define BOOT_FAILURE_LIMIT 3u
#define METADATA_FORMAT_VERSION 2u
#define PACKAGE_FORMAT_VERSION 1u
#define BOOT_API_VERSION 2u
#define PROTOCOL_VERSION 2u
#define MAX_PROTOCOL_PAYLOAD 152u
#define MAX_ENCODED_FRAME_SIZE 160u

enum BootState { BOOT_STATE_VALID_APPLICATION = 1, BOOT_STATE_UPDATE_REQUESTED,
                 BOOT_STATE_UPDATE_IN_PROGRESS };
enum ProtocolMessageType { PROTOCOL_HELLO = 1, PROTOCOL_INFO, PROTOCOL_BEGIN,
    PROTOCOL_READY, PROTOCOL_DATA, PROTOCOL_ACK, PROTOCOL_NACK, PROTOCOL_END,
    PROTOCOL_COMPLETE, PROTOCOL_ERROR, PROTOCOL_SET_ROLE };
enum AppRole { APP_ROLE_UNSET = 0u, APP_ROLE_RX = 1u, APP_ROLE_TX = 2u };

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t recordSize;
    uint32_t sequenceNumber;
    uint32_t state;
    uint32_t imageSize;
    uint32_t imageCrc32;
    uint32_t firmwareVersion;
    uint32_t appRole;
    uint32_t unconfirmedBootCount;
    uint32_t bootAttemptId;
    uint32_t confirmedBootAttemptId;
    uint32_t recordCrc32;
} BootMetadata;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t headerSize;
    uint32_t targetId;
    uint32_t applicationAddress;
    uint32_t imageSize;
    uint32_t firmwareVersion;
    uint32_t imageCrc32;
    uint32_t headerCrc32;
} FirmwarePackageHeader;

typedef struct { uint32_t magic; uint32_t bootAttemptId; uint32_t appRole; } BootHandoff;
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    int (*confirmBoot)(uint32_t bootAttemptId);
    int (*requestUpdate)(void);
} BootloaderApi;

static UART_Handle updateUart;

static int isValidAppRole(uint32_t role)
{
    return role == APP_ROLE_UNSET || role == APP_ROLE_RX || role == APP_ROLE_TX;
}

static uint32_t calculateCrc32(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = ~0u;
    uint32_t bitIndex;
    while (length--) {
        crc ^= *bytes++;
        for (bitIndex = 0; bitIndex < 8u; bitIndex++) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

static uint16_t calculateCrc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint8_t bitIndex;
    while (length--) {
        crc ^= (uint16_t)*data++ << 8;
        for (bitIndex = 0; bitIndex < 8u; bitIndex++) {
            crc = (crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1;
        }
    }
    return crc;
}

static int isErasedRecord(const BootMetadata *record)
{
    const uint32_t *words = (const uint32_t *)record;
    uint32_t wordIndex;
    for (wordIndex = 0; wordIndex < sizeof(*record) / sizeof(uint32_t); wordIndex++) {
        if (words[wordIndex] != ~0u) return 0;
    }
    return 1;
}

static int isValidMetadataRecord(const BootMetadata *record)
{
    return record->magic == METADATA_MAGIC &&
           record->formatVersion == METADATA_FORMAT_VERSION &&
           record->recordSize == sizeof(*record) &&
           record->recordCrc32 == calculateCrc32(record, sizeof(*record) - 4u);
}

/* Handles normal sequence growth and the UINT32_MAX -> 0 wraparound. */
static int isSequenceNewer(uint32_t candidate, uint32_t reference)
{
    return candidate != reference && (uint32_t)(candidate - reference) < 0x80000000u;
}

static const BootMetadata *findNewestRecordInPage(uint32_t pageAddress)
{
    const BootMetadata *records = (const BootMetadata *)pageAddress;
    const BootMetadata *newestRecord = NULL;
    uint32_t recordIndex;
    for (recordIndex = 0; recordIndex < FLASH_PAGE_SIZE / sizeof(BootMetadata); recordIndex++) {
        const BootMetadata *record = &records[recordIndex];
        if (isErasedRecord(record)) break;
        if (isValidMetadataRecord(record) && (!newestRecord ||
            isSequenceNewer(record->sequenceNumber, newestRecord->sequenceNumber) ||
            record->sequenceNumber == newestRecord->sequenceNumber)) {
            /* Equal sequence can only arise after an interrupted/retried
             * append.  Prefer the later physical record, never the stale one. */
            newestRecord = record;
        }
    }
    return newestRecord;
}

static const BootMetadata *findNewestMetadataRecord(void)
{
    const BootMetadata *primary = findNewestRecordInPage(METADATA_PAGE_PRIMARY);
    const BootMetadata *secondary = findNewestRecordInPage(METADATA_PAGE_SECONDARY);
    if (!primary) return secondary;
    if (!secondary) return primary;
    if (isSequenceNewer(secondary->sequenceNumber, primary->sequenceNumber)) return secondary;
    if (isSequenceNewer(primary->sequenceNumber, secondary->sequenceNumber)) return primary;
    /* A tie is a recovery case; primary is the deterministic journal owner. */
    return primary;
}

static int readBootMetadata(BootMetadata *metadata)
{
    const BootMetadata *storedMetadata = findNewestMetadataRecord();
    if (!storedMetadata) return -1;
    *metadata = *storedMetadata;
    return 0;
}

static void setDefaultMetadata(BootMetadata *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = METADATA_MAGIC;
    metadata->formatVersion = METADATA_FORMAT_VERSION;
    metadata->recordSize = sizeof(*metadata);
    metadata->state = BOOT_STATE_UPDATE_IN_PROGRESS;
    metadata->appRole = APP_ROLE_UNSET;
}

static int findFirstErasedRecord(uint32_t pageAddress, uint32_t *recordIndex)
{
    const BootMetadata *records = (const BootMetadata *)pageAddress;
    for (*recordIndex = 0; *recordIndex < FLASH_PAGE_SIZE / sizeof(BootMetadata);
         (*recordIndex)++) {
        if (isErasedRecord(&records[*recordIndex])) return 0;
    }
    return -1;
}

static int appendMetadataRecord(BootMetadata *metadata)
{
    const BootMetadata *newestRecord = findNewestMetadataRecord();
    uint32_t pageAddress;
    uint32_t recordIndex;
    BootMetadata recordToWrite;
    metadata->magic = METADATA_MAGIC;
    metadata->formatVersion = METADATA_FORMAT_VERSION;
    metadata->recordSize = sizeof(*metadata);
    metadata->sequenceNumber = newestRecord ? newestRecord->sequenceNumber + 1u : 1u;
    metadata->recordCrc32 = calculateCrc32(metadata, sizeof(*metadata) - 4u);
    for (pageAddress = METADATA_PAGE_PRIMARY; pageAddress <= METADATA_PAGE_SECONDARY;
         pageAddress += FLASH_PAGE_SIZE) {
        if (!findFirstErasedRecord(pageAddress, &recordIndex)) goto writeRecord;
    }
    /* Keep one valid journal page while rotating the other page. */
    pageAddress = (newestRecord && (uint32_t)newestRecord >= METADATA_PAGE_SECONDARY) ?
                  METADATA_PAGE_PRIMARY : METADATA_PAGE_SECONDARY;
    if (FlashSectorErase(pageAddress)) return -1;
    recordIndex = 0;
writeRecord:
    /* The CRC is programmed last, so interrupted writes are invalid records. */
    recordToWrite = *metadata;
    recordToWrite.recordCrc32 = ~0u;
    if (FlashProgram((uint8_t *)&recordToWrite,
                     pageAddress + recordIndex * sizeof(recordToWrite),
                     sizeof(recordToWrite) - 4u) ||
        FlashProgram((uint8_t *)&metadata->recordCrc32,
                     pageAddress + recordIndex * sizeof(recordToWrite) +
                     sizeof(recordToWrite) - 4u, 4u)) return -1;
    /* CC1310 must not immediately fetch/check flash in this programming path:
     * the flash cache can still expose stale data.  The journal reader checks
     * this CRC before every later use and therefore fails closed after reset. */
    return 0;
}

static int confirmApplicationBoot(uint32_t bootAttemptId)
{
    BootMetadata metadata;
    if (readBootMetadata(&metadata) || metadata.bootAttemptId != bootAttemptId ||
        metadata.confirmedBootAttemptId == bootAttemptId) return -1;
    metadata.confirmedBootAttemptId = bootAttemptId;
    if (metadata.unconfirmedBootCount) metadata.unconfirmedBootCount--;
    return appendMetadataRecord(&metadata);
}

static int requestFirmwareUpdate(void)
{
    BootMetadata metadata;
    if (readBootMetadata(&metadata)) setDefaultMetadata(&metadata);
    metadata.state = BOOT_STATE_UPDATE_REQUESTED;
    return appendMetadataRecord(&metadata);
}

/* The app reads this fixed flash address; it is the only bootloader ABI. */
#pragma DATA_SECTION(bootloaderApi, ".boot_api")
const BootloaderApi bootloaderApi = {
    BOOT_API_MAGIC, BOOT_API_VERSION, 0, confirmApplicationBoot, requestFirmwareUpdate
};

static uint16_t cobsEncode(const uint8_t *source, uint16_t sourceLength, uint8_t *destination)
{
    uint16_t sourceIndex = 0, destinationIndex = 1, codeIndex = 0;
    uint8_t code = 1;
    destination[0] = 0;
    while (sourceIndex < sourceLength) {
        if (source[sourceIndex] == 0) {
            destination[codeIndex] = code;
            codeIndex = destinationIndex++;
            code = 1;
        } else {
            destination[destinationIndex++] = source[sourceIndex];
            if (++code == 0xFFu) {
                destination[codeIndex] = code;
                codeIndex = destinationIndex++;
                code = 1;
            }
        }
        sourceIndex++;
    }
    destination[codeIndex] = code;
    return destinationIndex;
}

static int cobsDecode(uint8_t *buffer, uint16_t encodedLength, uint16_t *decodedLength)
{
    uint16_t readIndex = 0, writeIndex = 0;
    while (readIndex < encodedLength) {
        uint8_t code = buffer[readIndex++];
        uint8_t copyIndex;
        if (!code || readIndex + code - 1u > encodedLength) return -1;
        for (copyIndex = 1; copyIndex < code; copyIndex++) buffer[writeIndex++] = buffer[readIndex++];
        if (code != 0xFFu && readIndex < encodedLength) buffer[writeIndex++] = 0;
    }
    *decodedLength = writeIndex;
    return 0;
}

static void writeProtocolFrame(uint8_t messageType, uint16_t sequenceNumber,
                               const uint8_t *payload, uint16_t payloadLength)
{
    uint8_t decodedFrame[MAX_ENCODED_FRAME_SIZE];
    uint8_t encodedFrame[MAX_ENCODED_FRAME_SIZE + 2u];
    uint8_t delimiter = 0;
    uint16_t frameLength = 6u + payloadLength;
    uint16_t frameCrc;
    decodedFrame[0] = PROTOCOL_VERSION;
    decodedFrame[1] = messageType;
    decodedFrame[2] = (uint8_t)sequenceNumber;
    decodedFrame[3] = (uint8_t)(sequenceNumber >> 8);
    decodedFrame[4] = (uint8_t)payloadLength;
    decodedFrame[5] = (uint8_t)(payloadLength >> 8);
    if (payloadLength) memcpy(&decodedFrame[6], payload, payloadLength);
    frameCrc = calculateCrc16(decodedFrame, frameLength);
    decodedFrame[frameLength++] = (uint8_t)frameCrc;
    decodedFrame[frameLength++] = (uint8_t)(frameCrc >> 8);
    frameLength = cobsEncode(decodedFrame, frameLength, encodedFrame);
    UART_write(updateUart, encodedFrame, frameLength);
    UART_write(updateUart, &delimiter, 1);
}

static void readProtocolFrame(uint8_t *messageType, uint16_t *sequenceNumber,
                              uint8_t *payload, uint16_t *payloadLength)
{
    uint8_t encodedFrame[MAX_ENCODED_FRAME_SIZE];
    uint8_t receivedByte;
    uint16_t encodedLength = 0;
    for (;;) {
        uint16_t decodedLength, expectedLength, receivedCrc;
        UART_read(updateUart, &receivedByte, 1);
        if (receivedByte != 0) {
            if (encodedLength < sizeof(encodedFrame)) encodedFrame[encodedLength++] = receivedByte;
            else encodedLength = 0;
            continue;
        }
        if (!encodedLength || cobsDecode(encodedFrame, encodedLength, &decodedLength) || decodedLength < 8u) {
            encodedLength = 0;
            continue;
        }
        expectedLength = 8u + encodedFrame[4] + ((uint16_t)encodedFrame[5] << 8);
        receivedCrc = encodedFrame[decodedLength - 2u] | ((uint16_t)encodedFrame[decodedLength - 1u] << 8);
        if (decodedLength == expectedLength && encodedFrame[0] == PROTOCOL_VERSION &&
            receivedCrc == calculateCrc16(encodedFrame, decodedLength - 2u)) {
            *messageType = encodedFrame[1];
            *sequenceNumber = encodedFrame[2] | ((uint16_t)encodedFrame[3] << 8);
            *payloadLength = encodedFrame[4] | ((uint16_t)encodedFrame[5] << 8);
            if (*payloadLength) memcpy(payload, &encodedFrame[6], *payloadLength);
            return;
        }
        encodedLength = 0;
    }
}

static void storeUint32Le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static uint32_t loadUint32Le(const uint8_t *source)
{
    return source[0] | ((uint32_t)source[1] << 8) | ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static int eraseApplicationFlash(void)
{
    uint32_t address;
    for (address = APPLICATION_FLASH_START; address < APPLICATION_FLASH_END; address += FLASH_PAGE_SIZE) {
        if (FlashSectorErase(address)) return -1;
    }
    return 0;
}

static int isApplicationImageValid(const BootMetadata *metadata)
{
    const uint32_t *vectorTable = (const uint32_t *)APPLICATION_FLASH_START;
    return metadata->imageSize &&
           metadata->imageSize <= APPLICATION_FLASH_END - APPLICATION_FLASH_START &&
           vectorTable[0] >= APPLICATION_RAM_START && vectorTable[0] < APPLICATION_RAM_END &&
           (vectorTable[1] & 1u) && vectorTable[1] >= APPLICATION_FLASH_START &&
           vectorTable[1] < APPLICATION_FLASH_END &&
           calculateCrc32(vectorTable, metadata->imageSize) == metadata->imageCrc32;
}

static void startApplication(void)
{
    /* Stop bootloader interrupts before installing the app vector table. */
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 1u;
    UART_close(updateUart);
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 2u;
    CPUcpsid();
    HWREG(NVIC_ST_CTRL) = 0;
    HWREG(NVIC_DIS0) = 0xFFFFFFFFu;
    HWREG(NVIC_DIS1) = 0xFFFFFFFFu;
    HWREG(NVIC_UNPEND0) = 0xFFFFFFFFu;
    HWREG(NVIC_UNPEND1) = 0xFFFFFFFFu;
    HWREG(NVIC_VTABLE) = APPLICATION_FLASH_START;
    *(volatile uint32_t *)BOOT_DEBUG_RAM_ADDRESS = 3u;
    __asm(" dsb");
    __asm(" isb");
    /* A direct handoff must emulate the interrupt state after reset.  The
     * bootloader has disabled all sources and cleared every pending IRQ above,
     * so restoring PRIMASK here cannot dispatch a stale bootloader interrupt.
     * Keeping PRIMASK set would leak bootloader state into the app and prevent
     * its early SYS/BIOS startup code from observing normal reset semantics. */
    CPUcpsie();
    /* Do not return after replacing SP; the application's reset handler owns it.
     * Reload the vectors here: UART_close() above is a normal C call and may
     * overwrite r0/r1, so passing the vector values as function arguments is
     * unsafe at this final assembly transition. */
    __asm(" movw r2, #0x8000\n"
          " ldr r0, [r2, #0]\n"
          " ldr r1, [r2, #4]\n"
          " mov sp, r0\n"
          " bx r1");
}

static int isValidPackageHeader(const FirmwarePackageHeader *header)
{
    return header->magic == PACKAGE_MAGIC && header->formatVersion == PACKAGE_FORMAT_VERSION &&
           header->headerSize == sizeof(*header) && header->targetId == FIRMWARE_TARGET_ID &&
           header->applicationAddress == APPLICATION_FLASH_START && header->imageSize &&
           header->imageSize <= APPLICATION_FLASH_END - APPLICATION_FLASH_START &&
           !(header->imageSize & 3u) &&
           header->headerCrc32 == calculateCrc32(header, sizeof(*header) - 4u);
}

static void runFirmwareUpdate(BootMetadata *metadata)
{
    uint8_t messageType, payload[MAX_PROTOCOL_PAYLOAD], response[24];
    uint16_t sequenceNumber, payloadLength;
    FirmwarePackageHeader packageHeader;
    uint32_t bytesWritten = 0;
    uint32_t requestedRole = APP_ROLE_UNSET;
    int updateActive = 0;
    for (;;) {
        readProtocolFrame(&messageType, &sequenceNumber, payload, &payloadLength);
        if (messageType == PROTOCOL_HELLO && !payloadLength) {
            storeUint32Le(&response[0], FIRMWARE_TARGET_ID);
            storeUint32Le(&response[4], metadata->state);
            storeUint32Le(&response[8], APPLICATION_FLASH_END - APPLICATION_FLASH_START);
            storeUint32Le(&response[12], metadata->imageSize);
            storeUint32Le(&response[16], metadata->firmwareVersion);
            storeUint32Le(&response[20], metadata->appRole);
            writeProtocolFrame(PROTOCOL_INFO, sequenceNumber, response, sizeof(response));
        } else if (messageType == PROTOCOL_BEGIN && payloadLength == sizeof(packageHeader) + 4u) {
            memcpy(&packageHeader, payload, sizeof(packageHeader));
            requestedRole = loadUint32Le(&payload[sizeof(packageHeader)]);
            if (!isValidPackageHeader(&packageHeader) || !isValidAppRole(requestedRole)) {
                writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
                continue;
            }
            /* Record the update state before erase, so loss of power is recoverable. */
            setDefaultMetadata(metadata);
            if (appendMetadataRecord(metadata)) {
                writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
                continue;
            }
            if (eraseApplicationFlash()) {
                writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
                continue;
            }
            bytesWritten = 0;
            updateActive = 1;
            storeUint32Le(response, bytesWritten);
            writeProtocolFrame(PROTOCOL_READY, sequenceNumber, response, 4);
        } else if (messageType == PROTOCOL_DATA && updateActive && payloadLength >= 4u) {
            uint32_t offset = loadUint32Le(payload);
            uint16_t dataLength = payloadLength - 4u;
            if (!dataLength || dataLength > 128u || (dataLength & 3u) ||
                offset + dataLength > packageHeader.imageSize) {
                /* Never acknowledge a malformed DATA payload: ACK is reserved
                 * for a valid write or a valid duplicate retransmission. */
                writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
            } else {
                if (offset == bytesWritten &&
                    !FlashProgram(&payload[4], APPLICATION_FLASH_START + offset, dataLength)) {
                    bytesWritten += dataLength;
                }
                storeUint32Le(response, bytesWritten);
                writeProtocolFrame(offset <= bytesWritten ? PROTOCOL_ACK : PROTOCOL_NACK,
                                   sequenceNumber, response, 4);
            }
        } else if (messageType == PROTOCOL_END && updateActive && !payloadLength &&
                   bytesWritten == packageHeader.imageSize &&
                   calculateCrc32((const void *)APPLICATION_FLASH_START, packageHeader.imageSize) ==
                   packageHeader.imageCrc32) {
            metadata->state = BOOT_STATE_VALID_APPLICATION;
            metadata->imageSize = packageHeader.imageSize;
            metadata->imageCrc32 = packageHeader.imageCrc32;
            metadata->firmwareVersion = packageHeader.firmwareVersion;
            metadata->appRole = requestedRole;
            metadata->unconfirmedBootCount = 0;
            metadata->bootAttemptId = 0;
            metadata->confirmedBootAttemptId = 0;
            if (appendMetadataRecord(metadata)) {
                /* Do not claim success while the boot decision remains stale. */
                writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
            } else {
                writeProtocolFrame(PROTOCOL_COMPLETE, sequenceNumber, NULL, 0);
                SysCtrlSystemReset();
            }
        } else if (messageType == PROTOCOL_SET_ROLE && payloadLength == 4u &&
                   isValidAppRole(loadUint32Le(payload)) && isApplicationImageValid(metadata)) {
            metadata->state = BOOT_STATE_VALID_APPLICATION;
            metadata->appRole = loadUint32Le(payload);
            if (appendMetadataRecord(metadata)) {
                writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
            } else {
                writeProtocolFrame(PROTOCOL_COMPLETE, sequenceNumber, NULL, 0);
                SysCtrlSystemReset();
            }
        } else {
            writeProtocolFrame(PROTOCOL_ERROR, sequenceNumber, NULL, 0);
        }
    }
}

void *mainThread(void *argument)
{
    UART_Params uartParameters;
    BootMetadata metadata;
    (void)argument;
    GPIO_init();
    UART_init();
    GPIO_setConfig(Board_GPIO_LED0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
    UART_Params_init(&uartParameters);
    uartParameters.baudRate = 115200;
    uartParameters.readDataMode = UART_DATA_BINARY;
    uartParameters.writeDataMode = UART_DATA_BINARY;
    uartParameters.readReturnMode = UART_RETURN_FULL;
    uartParameters.readEcho = UART_ECHO_OFF;
    updateUart = UART_open(Board_UART0, &uartParameters);
    if (!updateUart) for (;;) {}
    if (readBootMetadata(&metadata)) setDefaultMetadata(&metadata);
    if (metadata.state != BOOT_STATE_VALID_APPLICATION ||
        metadata.unconfirmedBootCount >= BOOT_FAILURE_LIMIT ||
        !isApplicationImageValid(&metadata)) runFirmwareUpdate(&metadata);
    /* Increment before starting the app.  App confirmation decrements it. */
    metadata.unconfirmedBootCount++;
    metadata.bootAttemptId++;
    if (appendMetadataRecord(&metadata)) {
        /* A failed boot-attempt record must never be followed by an untracked
         * app handoff.  Keep this boot in the updater so it can be recovered. */
        runFirmwareUpdate(&metadata);
    }
    ((volatile BootHandoff *)BOOT_HANDOFF_RAM_ADDRESS)->magic = BOOT_HANDOFF_MAGIC;
    ((volatile BootHandoff *)BOOT_HANDOFF_RAM_ADDRESS)->bootAttemptId = metadata.bootAttemptId;
    ((volatile BootHandoff *)BOOT_HANDOFF_RAM_ADDRESS)->appRole = metadata.appRole;
    startApplication();
    for (;;) {}
}
