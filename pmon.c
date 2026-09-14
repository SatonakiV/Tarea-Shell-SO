#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "pmon.h"


int leer_proc_stat(pid_t pid, ProcStat *info){
    
    // string donde se almacena la ruta del /proc
    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%ld/stat", (long)pid);
    FILE *file = fopen(ruta,"r");

    /** 
     * Condicional para manejar la desaparicion del archivo en ruta /proc
     * para pmon no es necesariamente un error el que el archivo desaparezca,
     * por lo que solo se notificara con un -1
     */

    if(file == NULL) {
        return -1;
    }

    char *linea = NULL;
    size_t capacidad = 0;

    // verificacion si es que la linea completa de /proc/PID/stat desaparece, ocurre un error al leerla
    // o se leyo todo el archivo (EOF)
    if(getline(&linea, &capacidad, file) == -1){
        fclose(file);
        free(linea);
        return -1;
    }

    fclose(file);


    // ----- parseo de /proc/PID/stat
    char *fin_nombre = strrchr(linea, ')');

    if (fin_nombre == NULL) {
        free(linea);
        return -1;
    }


    char state;
    unsigned long utime;
    unsigned long stime;

    // el formato sera
    // "PID (comm) state  4  5  6  7  8  9  10 11 12 13  utime stime"
    // el % * s, con el * entremedio, sirve para -leer pero ignorar- los datos, pues esos campos no nos interesa almacenarlos
    int leidos = sscanf(fin_nombre + 1, " %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu", 
                        &state, &utime, &stime);

    
    // esperamos que lea 3 especificamente el sscanf, que son state, utime, stime
    if(leidos != 3){
        free(linea);
        return -1;
    }

    info->state = state;
    info->utime = utime;
    info->stime = stime;

    free(linea);

    return 0;

}


int leer_proc_status(pid_t pid, unsigned long *rss_kb){
    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%ld/status", (long)pid);
    FILE *file = fopen(ruta,"r");
    
    if(file == NULL) {
        return -1;
    };

    // ----parseo de /proc/PID/status
    char *linea = NULL;
    size_t capacidad = 0;
    int encontrado = 0;

    while(getline(&linea, &capacidad, file) != -1){
        
        // Si la linea comienza con VmRSS en sus 6 primeros caracteres, 
        // y si podemos extraer un unsigned long, se encontro el rss_kb
        if((strncmp(linea, "VmRSS:", 6) == 0) && 
            (sscanf(linea, "VmRSS: %lu kB", rss_kb) == 1)){
            encontrado = 1;
            break;
        };
    }

    free(linea);
    fclose(file);

    if (encontrado == 0) {
        return -1;
    }

    return 0;

}


int tomar_muestra(pid_t pid, MuestraProceso *muestra){
    
    ProcStat stat;

    // volvemos a comprobar errores para invalidar la muestra en caso de que el proceso termine
    if (leer_proc_stat(pid, &stat) == -1) {
        return -1;
    }

    unsigned long rss_kb;

    if (leer_proc_status(pid, &rss_kb) == -1) {
        return -1;
    }

    muestra->pid = pid;
    muestra->state = stat.state;
    muestra->cpu_ticks = stat.utime + stat.stime;
    muestra->rss_kb = rss_kb;

    return 0;
}


double calcular_cpu(unsigned long ticks_anterior, unsigned long ticks_actual,double dif_tiempo){
    
    long ticks_por_segundo = sysconf(_SC_CLK_TCK);

    // evitar hacer calculo invalido
    if (ticks_por_segundo <= 0 || dif_tiempo <= 0.0) {
        return 0.0;
    }

    unsigned long dif_ticks = ticks_actual - ticks_anterior;

    double cpu_time = (double)dif_ticks / (double)ticks_por_segundo;

    double porcentaje = (cpu_time / dif_tiempo) * 100.0;

    return porcentaje;
}


double calcular_dif_tiempo(struct timespec anterior, struct timespec actual){
    
    double dif_segundos = (double)(actual.tv_sec - anterior.tv_sec);

    double dif_nanosegundos = (double)(actual.tv_nsec - anterior.tv_nsec) / 1000000000.0;

    return dif_segundos + dif_nanosegundos;
}

int get_intervalo_segundos(const char *argumento, unsigned int *intervalo){

    if (intervalo == NULL) {
        return -1;
    }

    if (argumento == NULL) {
        *intervalo = 2;
        return 0;
    }

    char *fin;
    long valor = strtol(argumento, &fin, 10);

    if (argumento == fin || *fin != '\0' || valor <= 0 || valor > UINT_MAX) {
        return -1;
    }

    *intervalo = (unsigned int)valor;

    return 0;
}