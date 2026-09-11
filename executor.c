#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "executor.h"

static int apply_redirections(const Command *command) {
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

        // el modo 0644 solo lo usa el kernel si O_CREAT tiene que crear el archivo
        fd = open(redir->filename, flags, 0644);

        if (fd == -1) {
            fprintf(stderr, "mishell: %s: %s\n", redir->filename, strerror(errno));

            return -1;
        }

        // open() devuelve el fd libre mas bajo: si ya es el destino, el close cerraria lo que se acaba de montar
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

static pid_t spawn_command(Command *command) {
    pid_t pid;

    // el hijo hereda copia del buffer
    fflush(NULL);

    pid = fork();

    if(pid == -1) {
        perror("fork");
        
        return -1;
    }

    if(pid == 0) {
        if(apply_redirections(command) == -1) {
            // el hijo es una copia de la shell, no puede volver al ciclo del prompt
            _exit(1);
        }
        
        execvp(command -> argv[0], command -> argv);

        fprintf(stderr, "mishell: %s: %s \n", command -> argv[0], strerror(errno));

        // _exit y no exit: exit correria los atexit() y vaciaria los buffers de stdio heredados del padre, duplicando su salida.
        // ENOENT = no se encontro (127); cualquier otro error = existe pero no se puede ejecutar (126).
        _exit(errno == ENOENT ? EXEC_NOT_FOUND : EXEC_CANNOT_EXECUTE);
    }

    return pid;
}

static int wait_for_child(pid_t pid) {
    int status;

    // una senal interrumpe waitpid sin que el hijo haya muerto
    while(waitpid(pid, &status, 0) == -1) {
        if(errno == EINTR) {
            continue;
        }

        perror("waitpid");
        
        return -1;
    }

    if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if(WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

int execute_pipeline(const Pipeline *pipeline, int *status) {
    Command *command;
    pid_t pid;
    int code;

    if(pipeline == NULL || pipeline -> command_count == 0) {
        return 0;
    }

    if(pipeline -> command_count > 1) {
        fprintf(stderr, "pipes todavia no implementados \n");
        
        return -1;
    }

    if(pipeline -> background) {
        fprintf(stderr, "background todavia no implementado \n");

        return -1;
    }

    command = &pipeline -> commands[0];

    if(command -> argv == NULL || command -> argc == 0) {
        return 0;
    }

    pid = spawn_command(command);

    if(pid == -1) {
        return -1;
    }

    code = wait_for_child(pid);

    if(code == -1) {
        return -1;
    }

    if(status != NULL) {
        *status = code;
    }

    return 0;
}
