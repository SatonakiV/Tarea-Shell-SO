#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "executor.h"
#include "jobs.h"

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

// Cierra los dos extremos de todos los pipes. La usan el padre (tras el ciclo de fork) y cada hijo (tras sus dup2): un extremo de escritura abierto de mas
// impide que el lector reciba EOF y el pipeline se cuelga
static void close_all_pipes(int (*pipes)[2], size_t count) {
    for (size_t i = 0; i < count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static pid_t spawn_command(const Command *command, int (*pipes)[2], size_t pipe_count, size_t index, size_t command_count) {
    pid_t pid = fork();

    if(pid == -1) {
        perror("mishell: fork");

        return -1;
    }

    if(pid == 0) {

        sigset_t sigchld_mask;
        sigemptyset(&sigchld_mask);
        sigaddset(&sigchld_mask, SIGCHLD);
        sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);


        // el hijo i lee del pipe i-1 y escribe al pipe i
        if (index > 0 && dup2(pipes[index - 1][0], STDIN_FILENO) == -1) {
            perror("mishell: dup2");
            _exit(1);
        }

        if (index + 1 < command_count && dup2(pipes[index][1], STDOUT_FILENO) == -1) {
            perror("mishell: dup2");
            _exit(1);
        }

        // las redirecciones explicitas van despues de los pipes: asi un '>' le gana al pipe
        if(apply_redirections(command) == -1) {
            // el hijo es una copia de la shell, no puede volver al ciclo del prompt
            _exit(1);
        }

        // cerrar todos los extremos originales: 0 y 1 ya apuntan a donde deben gracias a dup2
        // asi que lo que quede abierto solo sirve para impedir EOF
        close_all_pipes(pipes, pipe_count);

        execvp(command -> argv[0], command -> argv);

        fprintf(stderr, "mishell: %s: %s\n", command -> argv[0], strerror(errno));

        // _exit y no exit: exit correria los atexit() y vaciaria los buffers de stdio heredados del padre, duplicando su salida
        // ENOENT = no se encontro (127); otro error = existe pero no se puede ejecutar (126)
        _exit(errno == ENOENT ? EXEC_NOT_FOUND : EXEC_CANNOT_EXECUTE);
    }

    return pid;
}



int execute_pipeline(const Pipeline *pipeline, const char *line, int *status) {
    int (*pipes)[2] = NULL;
    pid_t *pids = NULL;
    size_t count;
    size_t pipe_count;
    size_t created = 0;
    size_t i;
    int code = 0;
    int result = 0;

    if(pipeline == NULL || pipeline -> command_count == 0) {
        return 0;
    }


    count = pipeline -> command_count;
    pipe_count = count - 1;

    for(i = 0; i < count; i++) {
        const Command *command = &pipeline -> commands[i];

        if(command -> argv == NULL || command -> argc == 0) {
            return 0;
        }
    }

    pids = malloc(count * sizeof(*pids));

    if(pids == NULL) {
        perror("mishell: malloc");

        return -1;
    }

    if(pipe_count > 0) {
        pipes = malloc(pipe_count * sizeof(*pipes));

        if(pipes == NULL) {
            perror("mishell: malloc");
            free(pids);

            return -1;
        }
    }

    for(i = 0; i < pipe_count; i++) {
        if(pipe(pipes[i]) == -1) {
            perror("mishell: pipe");

            // solo los que ya se crearon
            close_all_pipes(pipes, i);   
            free(pipes);
            free(pids);

            return -1;
        }
    }

    // una sola vez para todo el pipeline, no una por hijo 
    // el hijo hereda una copia del buffer y lo pendiente se imprimiria dos veces
    fflush(NULL);


    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);

    for(created = 0; created < count; created++) {
        pids[created] = spawn_command(&pipeline -> commands[created], pipes, pipe_count, created, count);

        if(pids[created] == -1) {
            result = -1;

            break;
        }
    }

    // el padre cierra todos sus extremos apenas termina de forkear. Si no, el ultimo
    // comando nunca recibiria EOF porque la shell seguiria siendo un escritor vivo
    close_all_pipes(pipes, pipe_count);
    free(pipes);

    int job_id = -1;
    pid_t last_pid = (created > 0) ? pids[created - 1] : -1;



    if (created > 0) {
        // Aunque el fork() de algun comando haya fallado a mitad de camino,
        // igual registramos los que SÍ se crearon para evitar que sus SIGCHLD futuros
        // no encuentrnen job asociado y nadie espere por ellos.
        job_id = jobs_add(pids, created, pipeline->background, line);
    }

    // Si jobs_add falló (tabla llena) y es foreground, esperar directamente con waitpid.
    // SIGCHLD sigue bloqueado para que el handler no robe los hijos antes de recogerlos.
    if (job_id == -1 && !pipeline->background && created > 0) {
        for (size_t i = 0; i < created; i++) {
            int wstatus;
            pid_t w;
            while ((w = waitpid(pids[i], &wstatus, 0)) == -1 && errno == EINTR)
                ;
            if (w > 0 && pids[i] == last_pid) {
                if (WIFEXITED(wstatus)) {
                    code = WEXITSTATUS(wstatus);
                } else if (WIFSIGNALED(wstatus)) {
                    code = 128 + WTERMSIG(wstatus);
                }
            }
        }
        jobs_unblock_sigchld(&old_mask);
        free(pids);
        if (status != NULL) {
            *status = code;
        }
        return result;
    }

    jobs_unblock_sigchld(&old_mask);

    free(pids);


    if (job_id == -1) { return result;}

    if (pipeline->background) {
        printf("[%d] %d\n", job_id, last_pid);
        fflush(stdout);
        return result;
    }

    // Foreground: duerme hasta que elhandler de sigchild marque este job como terminado
    code = jobs_wait_foreground(job_id);

    if (status != NULL) {
        *status = code;
    }

    return result;
}
