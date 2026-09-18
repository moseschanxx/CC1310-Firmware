#include "cli_core.h"
#include "cli_port.h"
#include "cli_radio.h"
#include "cli_bootloader.h"
#include "firmware_build.h"
#include "firmware_mode.h"
#include "firmware_tx.h"
#include "rf_packet_queue.h"

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <ti/drivers/UART.h>
#include <ti/sysbios/knl/Idle.h>
#include <ti/sysbios/knl/Task.h>

#include "Board.h"

#define CLI_BAUD_RATE       115200U
#define CLI_THREAD_STACK    2048U

static UART_Handle cliUart;
static pthread_mutex_t cliUartMutex;
extern void *firmware_rx_thread(void *arg0);
void *cliThread(void *arg0);
static void stack_command(int argc, char *argv[]);
static CliCommand runtimeCommands[] = {
    { "rx", "rx status | rx dump on|off", NULL },
    { "bootloader", "reboot into UART firmware updater", NULL },
    { "version", "show firmware version", NULL },
    { "stack", "show SYS/BIOS task stack high-water marks", stack_command }
};

static void version_command(int argc, char *argv[])
{
    (void)argv;
    if (argc != 1) {
        cli_error("ARG", "usage: version");
        return;
    }

    cli_ok("version=" FIRMWARE_VERSION);
}

static const char *stack_mode_name(Task_Mode mode)
{
    switch (mode) {
    case Task_Mode_RUNNING: return "running";
    case Task_Mode_READY: return "ready";
    case Task_Mode_BLOCKED: return "blocked";
    case Task_Mode_TERMINATED: return "terminated";
    case Task_Mode_INACTIVE: return "inactive";
    default: return "unknown";
    }
}

static const char *stack_task_name(Task_Handle task)
{
    const char *name = Task_Handle_name(task);
    Task_FuncPtr function = Task_getFunc(task, NULL, NULL);

    /*
     * POSIX pthread_create() creates the underlying SYS/BIOS Task at run
     * time.  Such tasks carry SYS/BIOS's placeholder instance label rather
     * than a useful XDC configuration-time name.  Prefer the stable name
     * assigned by this application from the thread entry function.
     */
    if (name != NULL && name[0] != '\0' &&
        strcmp(name, "{unknown-instance-name}") != 0) return name;
    if (function == (Task_FuncPtr)cliThread) return "cli";
    if (function == (Task_FuncPtr)firmware_rx_thread) return "radio_rx";
    if (function == (Task_FuncPtr)firmware_tx_thread) return "radio_tx";
    if (function == (Task_FuncPtr)rf_packet_print_thread) return "packet_print";
    if (function == (Task_FuncPtr)Idle_loop) return "idle";
    return "unnamed";
}

/*
 * Task_stat().used is the stack high-water mark: SYS/BIOS initializes Task
 * stacks with a fill pattern, then scans for the lowest byte touched.  It is
 * meaningful while Task.initStackFlag is enabled in the SYS/BIOS config.
 */
static void stack_command(int argc, char *argv[])
{
    Task_Handle task;
    Task_Stat stat;
    char message[96];

    (void)argv;
    if (argc != 1) {
        cli_error("ARG", "usage: stack");
        return;
    }

    cli_ok("stack: name priority used/size free mode");
    task = Task_Object_first();
    while (task != NULL) {
        Task_stat(task, &stat);
        (void)snprintf(message, sizeof(message),
                       "stack: %s %d %lu/%lu %lu %s",
                       stack_task_name(task),
                       (int)stat.priority,
                       (unsigned long)stat.used,
                       (unsigned long)stat.stackSize,
                       (unsigned long)(stat.stackSize - stat.used),
                       stack_mode_name(stat.mode));
        cli_ok(message);
        task = Task_Object_next((Task_Object *)task);
    }
}
void cli_cc1310_uart_write(const char *data, size_t length)
{
    if (cliUart != NULL && length != 0U) {
        pthread_mutex_lock(&cliUartMutex);
        (void)UART_write(cliUart, data, length);
        pthread_mutex_unlock(&cliUartMutex);
    }
}

void cli_port_write(const char *data, size_t length)
{
    cli_cc1310_uart_write(data, length);
}

void *cliThread(void *arg0)
{
    UART_Params params;
    char line[CLI_MAX_LINE_LENGTH];
    char input;
    int_fast32_t count;
    size_t lineLength = 0;
    uint8_t lineTooLong = 0;
    uint32_t readyCount = 0;
    char readyMessage[80];
    FirmwareRole role = (FirmwareRole)(uintptr_t)arg0;

    if (pthread_mutex_init(&cliUartMutex, NULL) != 0) for (;;) {}
    UART_init();
    UART_Params_init(&params);
    params.baudRate = CLI_BAUD_RATE;
    /* Read one byte at a time and recognize both terminal line endings here.
     * This avoids the driver's text-mode newline dependency and accepts CR,
     * LF, and CRLF from picocom and other terminal programs. */
    params.readDataMode = UART_DATA_BINARY;
    params.readMode = UART_MODE_BLOCKING;
    params.writeMode = UART_MODE_BLOCKING;
    params.readReturnMode = UART_RETURN_FULL;
    params.readEcho = UART_ECHO_OFF;
    cliUart = UART_open(Board_UART0, &params);
    if (cliUart == NULL) for (;;) {}

    runtimeCommands[0] = role == FIRMWARE_ROLE_TX ? cli_tx_command : cli_rx_command;
    runtimeCommands[1] = cli_bootloader_command;
    runtimeCommands[2].handler = version_command;
    cli_init(runtimeCommands, sizeof(runtimeCommands) / sizeof(runtimeCommands[0]));
    snprintf(readyMessage, sizeof(readyMessage), "version=" FIRMWARE_VERSION " role=%s",
             firmware_role_name(role));
    cli_ok(readyMessage);
    snprintf(readyMessage, sizeof(readyMessage),
             "cli=ready count=%lu commands=help,%s,bootloader,version",
             (unsigned long)readyCount++, firmware_role_name(role));
    cli_ok(readyMessage);

    for (;;) {
        count = UART_read(cliUart, &input, 1U);
        if (count != 1) continue;

        if (input == '\r' || input == '\n') {
            if (lineTooLong) {
                cli_error("LINE", "too_long");
            } else if (lineLength != 0U) {
                line[lineLength] = '\0';
                cli_process_line(line);
            }
            lineLength = 0;
            lineTooLong = 0;
        } else if (lineLength < sizeof(line) - 1U) {
            line[lineLength++] = input;
        } else {
            /* Keep consuming the overlong line, then report one clear error. */
            lineTooLong = 1;
        }
    }
}

int cli_cc1310_start(FirmwareRole role)
{
    pthread_t thread;
    pthread_attr_t attrs;
    struct sched_param priority;
    int status;

    pthread_attr_init(&attrs);
    priority.sched_priority = 1;
    status = pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    status |= pthread_attr_setschedparam(&attrs, &priority);
    status |= pthread_attr_setstacksize(&attrs, CLI_THREAD_STACK);
    if (status != 0) return -1;
    return pthread_create(&thread, &attrs, cliThread, (void *)(uintptr_t)role);
}
