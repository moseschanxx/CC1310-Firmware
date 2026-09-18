#include "cli_core.h"
#include "cli_port.h"

#include <string.h>

static const CliCommand *cliCommands;
static size_t cliCommandCount;

static void cli_print_command(const CliCommand *command)
{
    cli_port_write("OK ", 3U);
    cli_port_write(command->name, strlen(command->name));
    cli_port_write(" ", 1U);
    cli_port_write(command->usage, strlen(command->usage));
    cli_port_write("\r\n", 2U);
}

static void cli_help(int argc, char *argv[])
{
    size_t i;

    if (argc > 2) {
        cli_error("ARG", "usage: help [command]");
        return;
    }
    if (argc == 2) {
        if (strcmp(argv[1], "help") == 0) {
            cli_ok("help [command]");
            return;
        }
        for (i = 0; i < cliCommandCount; ++i) {
            if (strcmp(argv[1], cliCommands[i].name) == 0) {
                cli_print_command(&cliCommands[i]);
                return;
            }
        }
        cli_error("CMD", "unknown_command");
        return;
    }

    cli_ok("commands:");
    cli_ok("help [command]");
    for (i = 0; i < cliCommandCount; ++i) {
        cli_print_command(&cliCommands[i]);
    }
}

static void cli_write_line(const char *prefix, const char *message)
{
    cli_port_write(prefix, strlen(prefix));
    if (message != NULL && message[0] != '\0') {
        cli_port_write(" ", 1U);
        cli_port_write(message, strlen(message));
    }
    cli_port_write("\r\n", 2U);
}

void cli_ok(const char *message) { cli_write_line("OK", message); }

void cli_error(const char *code, const char *message)
{
    cli_port_write("ERR ", 4U);
    cli_port_write(code, strlen(code));
    if (message != NULL && message[0] != '\0') {
        cli_port_write(" ", 1U);
        cli_port_write(message, strlen(message));
    }
    cli_port_write("\r\n", 2U);
}

void cli_init(const CliCommand *commands, size_t commandCount)
{
    cliCommands = commands;
    cliCommandCount = commandCount;
}

void cli_process_line(char *line)
{
    char *argv[CLI_MAX_ARGUMENTS];
    int argc = 0;
    char *token;
    size_t i;

    token = strtok(line, " \t\r\n");
    while (token != NULL) {
        if (argc == (int)CLI_MAX_ARGUMENTS) {
            cli_error("ARG", "too_many_arguments");
            return;
        }
        argv[argc++] = token;
        token = strtok(NULL, " \t\r\n");
    }
    if (argc == 0) return;

    if (strcmp(argv[0], "help") == 0) {
        cli_help(argc, argv);
        return;
    }

    for (i = 0; i < cliCommandCount; ++i) {
        if (strcmp(argv[0], cliCommands[i].name) == 0) {
            cliCommands[i].handler(argc, argv);
            return;
        }
    }
    cli_error("CMD", "unknown_command; use help");
}
