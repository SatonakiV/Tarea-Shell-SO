#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    };

    char *linea = NULL;
    size_t capacidad = 0;

    // verificacion si es que la linea completa de /proc/PID/stat desaparece, ocurre un error al leerla
    // o se leyo todo el archivo (EOF)
    if(getline(&linea, &capacidad, file) == -1){
        fclose(file);
        free(linea);
        return -1;
    };

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

    
    // esperamos que lea 3 especificamente el sscanf, que son comm, utime, stime
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