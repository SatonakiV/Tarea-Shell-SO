#include <stdio.h>
#include <stdlib.h>

#include "pmon.h"

int main(int argc, char *argv[]){
    if (argc != 2) {
        fprintf(stderr, "Uso correcto: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);

    ProcStat info;

    if (leer_proc_stat(pid, &info) == -1) {
        fprintf(stderr, "Error, no se pudo leer /proc/%ld/stat\n", (long)pid);
        return 1;
    }

    printf("PID: %ld\n", (long)pid);
    printf("Comando: %s\n", info.comando);
    printf("Estado: %c\n", info.state);
    printf("utime: %lu\n", info.utime);
    printf("stime: %lu\n", info.stime);

    return 0;
}