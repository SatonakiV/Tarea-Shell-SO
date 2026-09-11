#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "executor.h"

static pid_t spawn_command(Command *command) {
    pid_t pid;

    fflush(NULL);

    pid = fork();

    if(pid == -1) {
        perror("fork");
        
        return -1;
    }

    if(pid == 0) {
        execvp(command -> argv[0], command -> argv);

        fprintf(stderr, "mishell: %s: %s \n", command -> argv[0], strerror(errno));

        _exit(errno == ENOENT ? EXEC_NOT_FOUND : EXEC_CANNOT_EXECUTE);
    }

    return pid;
}

static int wait_for_child(pid_t pid) {
    int status;

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
