#include <stdio.h>

#include "pmon.h"

int main(void){
    unsigned long anterior = 100;
    unsigned long actual = 140;
    double intervalo = 2.0;

    double cpu = calcular_cpu(anterior, actual, intervalo);

    printf("CPU aproximado: %.2f%%\n", cpu);

    return 0;
}