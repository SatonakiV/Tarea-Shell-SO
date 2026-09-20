#include <stdio.h>
#include <stdlib.h>
#include "pmon.h"

int main(int argc, char *argv[]){

    if (argc < 2) {
        fprintf(stderr, "Uso correcto: %s <pid> <otros pid si se quieren testear mas>\n", argv[0]);
        return 1;
    }

    size_t cantidad = argc - 1;
    pid_t pids[cantidad];

    // guardamos todos los PID entregados por argumento
    for (size_t i = 0; i < cantidad; i++) {
        pids[i] = (pid_t)strtol(argv[i + 1], NULL, 10);
    }

    // ejecutamos pmon con un intervalo de 2 segundos
    ejecutar_pmon(pids, cantidad, 2);

    return 0;
}