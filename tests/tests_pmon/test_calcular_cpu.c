#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "pmon.h"

int main(int argc, char *argv[]){
    if (argc != 2) {
        fprintf(stderr, "Uso correcto: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);

    MuestraProceso anterior;
    MuestraProceso actual;

    struct timespec tiempo_anterior;
    struct timespec tiempo_actual;

    clock_gettime(CLOCK_MONOTONIC, &tiempo_anterior);

    if (tomar_muestra(pid, &anterior) == -1) {
        fprintf(stderr, "Error, no se pudo tomar la primera muestra\n");
        return 1;
    }

    sleep(2);

    clock_gettime(CLOCK_MONOTONIC, &tiempo_actual);

    if (tomar_muestra(pid, &actual) == -1) {
        fprintf(stderr, "Error, no se pudo tomar la segunda muestra\n");
        return 1;
    }

    double dif_tiempo = calcular_dif_tiempo(tiempo_anterior, tiempo_actual);

    double cpu = calcular_cpu(anterior.cpu_ticks, actual.cpu_ticks, dif_tiempo);

    printf("PID: %ld\n", (long)pid);
    printf("Ticks anteriores: %lu\n", anterior.cpu_ticks);
    printf("Ticks actuales: %lu\n", actual.cpu_ticks);
    printf("Tiempo transcurrido: %.3f s\n", dif_tiempo);
    printf("CPU: %.2f%%\n", cpu);

    return 0;
}