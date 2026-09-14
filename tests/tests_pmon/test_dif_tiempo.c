#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "pmon.h"

int main(void){
    struct timespec anterior;
    struct timespec actual;

    clock_gettime(CLOCK_MONOTONIC, &anterior);

    sleep(2);

    clock_gettime(CLOCK_MONOTONIC, &actual);

    double diferencia =
        calcular_dif_tiempo(anterior, actual);

    printf("Tiempo transcurrido: %.3f segundos\n", diferencia);

    return 0;
    
}