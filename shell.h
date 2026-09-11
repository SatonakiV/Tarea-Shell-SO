#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

typedef enum {
    REDIR_INPUT,
    REDIR_OUTPUT,
    REDIR_APPEND
} RedirType;

typedef struct {
    RedirType type;
    char *filename;
} Redirection;

typedef struct {
    char **argv;
    size_t argc;
    Redirection *redirs;
    size_t redir_count;
} Command;

typedef struct {
    Command *commands;
    size_t command_count;
    int background;
} Pipeline;

int parse_line(const char *line, Pipeline *out, const char **error);
void free_pipeline(Pipeline *pipeline);

int run_shell(void);

#endif