#include <stdio.h>
#include <stdlib.h>

#include "pmon.h"

int main(int argc, char *argv[]){

    if (argc != 2) {
        fprintf(stderr, "Uso correcto: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);

    MuestraProceso muestra;

    if (tomar_muestra(pid, &muestra) == -1) {
        fprintf(stderr, "Error, no se pudo tomar la muestra del proceso\n");
        return 1;
    }

    printf("PID: %ld\n", (long)muestra.pid);
    printf("Comando: %s\n", muestra.comando);
    printf("CPU ticks: %lu\n", muestra.cpu_ticks);
    printf("RSS: %lu kB\n", muestra.rss_kb);

    return 0;
}