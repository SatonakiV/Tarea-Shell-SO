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

void free_pipeline(Pipeline *pipeline){
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

static int add_argument(Command *command, char *word){

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

static int add_redirection(Command *command, RedirType type, char *filename){
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

static TokenType next_token(const char **cursor, char **word){
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

    char *buffer = malloc(strlen(p) + 1);
    if (buffer == NULL) {
        return TOK_NOMEM;
    }
    size_t length = 0;
    char quote = '\0';
    while (*p != '\0') {
        if (quote == '\0' && (isspace((unsigned char)*p) || strchr("|<>&", *p))) {
            break;
        }
        if (*p == '\\' && quote != '\'') {
            p++;
            if (*p == '\0') {
                free(buffer);
                return TOK_UNSUPPORTED;
            }
            if (quote == '"' && *p != '"' && *p != '\\' &&
                *p != '$' && *p != '`') {
                buffer[length++] = '\\';
            }
            buffer[length++] = *p++;
        } else if (*p == '\'' || *p == '"') {
            if (quote == '\0') {
                quote = *p++;
            } else if (quote == *p) {
                quote = '\0';
                p++;
            } else {
                buffer[length++] = *p++;
            }
        } else {
            buffer[length++] = *p++;
        }
    }
    if (quote != '\0') {
        free(buffer);
        return TOK_UNSUPPORTED;
    }
    buffer[length] = '\0';
    *word = buffer;
    *cursor = p;
    return TOK_WORD;
}

int parse_line(const char *line, Pipeline *out, const char **error){
    const char *cursor = line;
    char *word = NULL;
    *out = (Pipeline){0};
    *error = NULL;

    for (;;) {
        TokenType token = next_token(&cursor, &word);
        if (token == TOK_NOMEM) {
            goto nomem;
        }
        if (token == TOK_UNSUPPORTED) {
            *error = "comillas o escape sin cerrar";
            goto fail;
        }
        if (token == TOK_END && out->command_count == 0) {
            return 0;
        }
        if (out->command_count == 0 && add_command(out) == -1) {
            goto nomem;
        }
        Command *command = &out->commands[out->command_count - 1];
        if (token == TOK_WORD) {
            if (add_argument(command, word) == -1) {
                goto nomem;
            }
            word = NULL;
        } else if (token == TOK_INPUT || token == TOK_OUTPUT || token == TOK_APPEND) {
            TokenType filename_token = next_token(&cursor, &word);
            if (filename_token == TOK_NOMEM) {
                goto nomem;
            }
            if (filename_token != TOK_WORD) {
                *error = "se esperaba un archivo despues de la redireccion";
                goto fail;
            }
            RedirType type = token == TOK_INPUT ? REDIR_INPUT :
                token == TOK_OUTPUT ? REDIR_OUTPUT : REDIR_APPEND;
            if (add_redirection(command, type, word) == -1) {
                goto nomem;
            }
            word = NULL;
        } else {
            if (command->argc == 0) {
                *error = "falta un comando";
                goto fail;
            }
            if (token == TOK_PIPE) {
                if (add_command(out) == -1) {
                    goto nomem;
                }
            } else if (token == TOK_BACKGROUND) {
                if (next_token(&cursor, &word) != TOK_END) {
                    *error = "& solo puede aparecer al final";
                    goto fail;
                }
                out->background = 1;
                return 0;
            } else {
                return 0;
            }
        }
    }

nomem:
    *error = "memoria insuficiente";
fail:
    free(word);
    free_pipeline(out);
    return -1;
}
