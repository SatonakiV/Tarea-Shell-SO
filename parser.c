#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"

typedef enum {
    TOK_NOMEM = -2,
    TOK_UNSUPPORTED = -1,
    TOK_END = 0,
    TOK_WORD,
    TOK_PIPE,
    TOK_INPUT,
    TOK_OUTPUT,
    TOK_APPEND,
    TOK_BACKGROUND
} TokenType;

void free_pipeline(Pipeline *pipeline)
{
    for (size_t i = 0; i < pipeline->command_count; i++) {
        Command *command = &pipeline->commands[i];
        for (size_t j = 0; j < command->argc; j++) {
            free(command->argv[j]);
        }
        free(command->argv);

        for (size_t j = 0; j < command->redir_count; j++) {
            free(command->redirs[j].filename);
        }
        free(command->redirs);
    }

    free(pipeline->commands);
    *pipeline = (Pipeline){0};
}

static int add_command(Pipeline *pipeline)
{
    size_t count = pipeline->command_count;
    Command *new_commands = realloc(
        pipeline->commands, (count + 1) * sizeof(*new_commands));

    if (new_commands == NULL) {
        return -1;
    }

    pipeline->commands = new_commands;
    pipeline->commands[count] = (Command){0};
    pipeline->command_count++;
    return 0;
}

static int add_argument(Command *command, char *word)
{

    char **new_argv = realloc(
        command->argv, (command->argc + 2) * sizeof(*new_argv));

    if (new_argv == NULL) {
        return -1;
    }

    command->argv = new_argv;
    command->argv[command->argc] = word;
    command->argc++;
    command->argv[command->argc] = NULL;
    return 0;
}

static int add_redirection(Command *command, RedirType type, char *filename)
{
    size_t count = command->redir_count;
    Redirection *new_redirs = realloc(
        command->redirs, (count + 1) * sizeof(*new_redirs));

    if (new_redirs == NULL) {
        return -1;
    }

    command->redirs = new_redirs;
    command->redirs[count].type = type;
    command->redirs[count].filename = filename;
    command->redir_count++;
    return 0;
}

static TokenType next_token(const char **cursor, char **word)
{
    const char *p = *cursor;
    *word = NULL;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        *cursor = p;
        return TOK_END;
    }

    switch (*p) {
        case '|':
            *cursor = p + 1;
            return TOK_PIPE;
        case '<':
            *cursor = p + 1;
            return TOK_INPUT;
        case '>':
            if (*(p + 1) == '>') {
                *cursor = p + 2;
                return TOK_APPEND;
            }
            *cursor = p + 1;
            return TOK_OUTPUT;
        case '&':
            *cursor = p + 1;
            return TOK_BACKGROUND;
        default:
            break;
    }







