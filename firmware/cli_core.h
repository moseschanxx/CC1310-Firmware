#ifndef CLI_CORE_H
#define CLI_CORE_H

#include <stddef.h>

#define CLI_MAX_LINE_LENGTH 128U
#define CLI_MAX_ARGUMENTS   8U

typedef void (*CliCommandHandler)(int argc, char *argv[]);

typedef struct {
    const char *name;
    const char *usage;
    CliCommandHandler handler;
} CliCommand;

void cli_init(const CliCommand *commands, size_t commandCount);
void cli_process_line(char *line);
void cli_ok(const char *message);
void cli_error(const char *code, const char *message);

#endif
