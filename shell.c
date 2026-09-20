#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "executor.h"
#include "shell.h"
#include "jobs.h"
#include "pmon.h"

static void show_prompt(void){

    jobs_notify_done();
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

static int builtin_pmon(const Command *command){

    if (command->argc > 2) {
        fprintf(stderr, "uso correcto: pmon [segundos]\n");
        return 1;
    }

    unsigned int interval;

    const char *argument = NULL;

    if (command->argc == 2) {
        argument = command->argv[1];
    }

    if (get_intervalo_segundos(argument, &interval) == -1) {
        fprintf(stderr, "pmon: intervalo invalido\n");
        return 1;
    }

    pid_t *pids = NULL;
    size_t count = 0;

    if (jobs_get_background_pids(&pids, &count) == -1) {
        fprintf(stderr, "pmon: no se pudieron obtener los procesos background\n");
        return 1;
    }

    if (count == 0) {
        printf("pmon: no hay procesos background activos\n");
        return 0;
    }

    int status = ejecutar_pmon(pids, count, interval);

    free(pids);

    if (status == -1) {
        return 1;
    }

    return 0;
}

// Aplica las redirecciones de un builtin guardando los fd originales para restaurarlos después.
// Devuelve 0 si todo fue bien, -1 si hubo error (y ya restauró lo que pudo).
static int apply_builtin_redirections(const Command *command, int saved_fds[], int *saved_count) {
    *saved_count = 0;

    for (size_t i = 0; i < command->redir_count; i++) {
        const Redirection *redir = &command->redirs[i];
        int flags;
        int target;
        int fd;

        switch (redir->type) {
            case REDIR_INPUT:
                flags = O_RDONLY;
                target = STDIN_FILENO;
                break;
            case REDIR_OUTPUT:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                target = STDOUT_FILENO;
                break;
            case REDIR_APPEND:
                flags = O_WRONLY | O_CREAT | O_APPEND;
                target = STDOUT_FILENO;
                break;
            default:
                fprintf(stderr, "mishell: tipo de redireccion desconocido\n");
                return -1;
        }

        // Guardar el fd original antes de sobreescribirlo (solo la primera vez para cada target)
        int already_saved = 0;
        for (int s = 0; s < *saved_count; s += 2) {
            if (saved_fds[s] == target) {
                already_saved = 1;
                break;
            }
        }
        if (!already_saved) {
            int backup = dup(target);
            if (backup == -1) {
                perror("mishell: dup");
                return -1;
            }
            saved_fds[*saved_count] = target;
            saved_fds[*saved_count + 1] = backup;
            *saved_count += 2;
        }

        fd = open(redir->filename, flags, 0644);
        if (fd == -1) {
            fprintf(stderr, "mishell: %s: %s\n", redir->filename, strerror(errno));
            return -1;
        }

        if (fd != target) {
            if (dup2(fd, target) == -1) {
                perror("mishell: dup2");
                close(fd);
                return -1;
            }
            close(fd);
        }
    }

    return 0;
}

// Restaura los fd originales guardados por apply_builtin_redirections.
static void restore_builtin_redirections(int saved_fds[], int saved_count) {
    for (int i = 0; i < saved_count; i += 2) {
        dup2(saved_fds[i + 1], saved_fds[i]);
        close(saved_fds[i + 1]);
    }
}

static int handle_builtin(const Pipeline *pipeline, int *should_exit, int *exit_code){
    // Los builtins en pipelines o background se delegan a execute_pipeline
    if (pipeline->command_count != 1 || pipeline->background) {
        return 0;
    }
    const Command *command = &pipeline->commands[0];

    // Aplicar redirecciones si las hay, guardando los fd originales
    int saved_fds[4]; // máximo 2 targets (stdin + stdout), cada uno ocupa 2 slots
    int saved_count = 0;
    int redir_ok = 1;

    if (command->redir_count != 0) {
        if (apply_builtin_redirections(command, saved_fds, &saved_count) == -1) {
            // Restaurar lo que se haya podido guardar y reportar error
            restore_builtin_redirections(saved_fds, saved_count);
            *exit_code = 1;
            return 1;
        }
        redir_ok = 1;
    }

    int handled = 0;

    if (strcmp(command->argv[0], "cd") == 0) {
        *exit_code = builtin_cd(command);
        handled = 1;
    } else if (strcmp(command->argv[0], "exit") == 0) {
        int status = builtin_exit(command, should_exit, exit_code);
        if (!*should_exit) {
            *exit_code = status;
        }
        handled = 1;
    } else if (strcmp(command->argv[0], "jobs") == 0) {
        jobs_list();
        *exit_code = 0;
        handled = 1;
    } else if (strcmp(command->argv[0], "pmon") == 0) {
        *exit_code = builtin_pmon(command);
        handled = 1;
    }

    // Restaurar fd originales si se aplicaron redirecciones
    if (redir_ok && saved_count > 0) {
        restore_builtin_redirections(saved_fds, saved_count);
    }

    return handled;
}

int run_shell(void){
    char *line = NULL;
    size_t capacity = 0;
    int should_exit = 0;
    int exit_code = 0;
    jobs_init();

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
            exit_code = (execute_pipeline(&pipeline, line, &status) == 0) ? status : 1;
        }
        free_pipeline(&pipeline);
    }
    free(line);
    return exit_code;
}
