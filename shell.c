#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "executor.h"
#include "shell.h"

static void show_prompt(void){

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

static int read_line(char **line, size_t *capacity){
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

static int builtin_cd(const Command *command){
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

static int builtin_exit(const Command *command, int *should_exit, int *exit_code){
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

static int handle_builtin(const Pipeline *pipeline, int *should_exit, int *exit_code){
    if (pipeline->command_count != 1 || pipeline->background) {
        return 0;
    }
    const Command *command = &pipeline->commands[0];
    if (command->redir_count != 0) {
        return 0;
    }
    if (strcmp(command->argv[0], "cd") == 0) {
        *exit_code = builtin_cd(command);
        return 1;
    }
    if (strcmp(command->argv[0], "exit") == 0) {
        int status = builtin_exit(command, should_exit, exit_code);
        if (!*should_exit) {
            *exit_code = status;
        }
        return 1;
    }
    return 0;
}

int run_shell(void){
    char *line = NULL;
    size_t capacity = 0;
    int should_exit = 0;
    int exit_code = 0;

    while (!should_exit) {
        int read_status = read_line(&line, &capacity);
        if (read_status <= 0) {
            if (read_status < 0) {
                exit_code = 1;
            }
            break;
        }
        Pipeline pipeline;
        const char *error;
        if (parse_line(line, &pipeline, &error) == -1) {
            fprintf(stderr, "sintaxis: %s\n", error);
            exit_code = 2;
            continue;
        }
        if (pipeline.command_count != 0 &&
            !handle_builtin(&pipeline, &should_exit, &exit_code)) {
            int status = 0;
            exit_code = (execute_pipeline(&pipeline, &status) == 0) ? status : 1;
        }
        free_pipeline(&pipeline);
    }
    free(line);
    return exit_code;
}
