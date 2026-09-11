#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"

static void show_prompt(void)
{

    char *directory = getcwd(NULL, 0);

    if (directory == NULL) {
        perror("getcwd");
        printf("mishell$ ");
    } else {
        printf("%s$ ", directory);
        free(directory);
    }

    fflush(stdout);
}

static int read_line(char **line, size_t *capacity)
{
    int interactive = isatty(STDIN_FILENO);

    for (;;) {
        if (interactive) {
            show_prompt();
        }

        errno = 0;
        ssize_t length = getline(line, capacity, stdin);

        if (length >= 0) {
            return 1;
        }

        if (errno == EINTR) {

            clearerr(stdin);
            continue;
        }

        if (feof(stdin)) {
            if (interactive) {
                putchar('\n');
            }
            return 0;
        }

        perror("getline");
        return -1;
    }
}

static int builtin_cd(const Command *command)
{
    const char *directory;

    if (command->argc > 2) {
        fprintf(stderr, "cd: demasiados argumentos\n");
        return 1;
    }

    if (command->argc == 1) {
        directory = getenv("HOME");
        if (directory == NULL || directory[0] == '\0') {
            fprintf(stderr, "cd: HOME no esta definido o esta vacio\n");
            return 1;
        }
    } else {
        directory = command->argv[1];
    }

    if (chdir(directory) == -1) {
        perror("cd");
        return 1;
    }

    return 0;
}

static int builtin_exit(
    const Command *command, int *should_exit, int *exit_code)
{
    long value = 0;

    if (command->argc > 2) {
        fprintf(stderr, "exit: demasiados argumentos\n");
        return 2;
    }

    if (command->argc == 2) {
        char *end = NULL;
        errno = 0;
        value = strtol(command->argv[1], &end, 10);

        if (errno == ERANGE || end == command->argv[1] || *end != '\0') {
            fprintf(stderr, "exit: se requiere un entero valido\n");
            return 2;
        }
    }

    value %= 256;
    if (value < 0) {
        value += 256;
    }

    *exit_code = (int)value;
    *should_exit = 1;
    return 0;
}

static int handle_builtin(
    const Pipeline *pipeline, int *should_exit, int *exit_code)
{
    if (pipeline->command_count != 1) {
        return 0;
    }
