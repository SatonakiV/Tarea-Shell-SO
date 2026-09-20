#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>

#include "pmon.h"


static volatile sig_atomic_t actualizar = 0;
static volatile sig_atomic_t salir_pmon = 0;

static void handler_alarma(int signal){
    (void)signal; // lo hacemos void unicamente para evitar el warning de compilacion de que no se esta usando la variable
    actualizar = 1;
}

static void handler_sigint(int signal){
    (void)signal;
    salir_pmon = 1;
}

static int configurar_SIGALRM(){

    struct sigaction sa = {0};
    sa.sa_handler = handler_alarma;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        return -1;
    }

    return 0;
}

static int configurar_SIGINT(struct sigaction *anterior){

    struct sigaction sa = {0};
    sa.sa_handler = handler_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // cuando salgamos de pmon queremos que SIGINT restaure su comportamiento original con CTRL + C,  
    if (sigaction(SIGINT, &sa, anterior) == -1) {
        return -1;
    }

    return 0;
}

// funcion auxiliar para dejar suspendido el proceso mientras se espera que llegue SIGALRM o SIGINT
static void esperar_actualizacion(){
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    // mientras no se solicite actualizar con la alarma o salir de pmon, se mantiene suspendido el proceso
    while(!actualizar && !salir_pmon){
        sigsuspend(&oldmask); // desbloquea atómicamente y espera
    }

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    if (actualizar) {
        actualizar = 0;
    }
}


// funcion auxiliar que permite mostrar la fila de los datos correspondientes a la muestra escogida del proceso y su %CPU calculado
// cada "-" es para alinear a la izquierda dados los espacios que se declaren
// cada numero es simplemente para dejar reservado cierta cantidad de caracteres por dato como minimo, si sobran se llena con espacios
static void mostrar_fila(MuestraProceso *muestra, double cpu){
    printf("%-8ld %-20s %-8c %-8.2f %lu\n", (long)muestra->pid, muestra->comando, muestra->state, cpu, muestra->rss_kb);
}

// funcion auxiliar para mostrar titulos de la tabla, siguiendo el mismo formato de los espacios que las filas
static void mostrar_titulos(){
    printf("%-8s %-20s %-8s %-8s %s\n", "PID", "COMANDO", "ESTADO", "%CPU", "RSS(KB)");
}

int leer_proc_stat(pid_t pid, ProcStat *info){
    
    // string donde se almacena la ruta del /proc/<PID>/status
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
    char *inicio_nombre = strchr(linea, '(');
    char *fin_nombre = strrchr(linea, ')');

    if (inicio_nombre == NULL || fin_nombre == NULL) {
        free(linea);
        return -1;
    }

    // restamos ambas direcciones de memoria para calcular cuantos datos hay entre '(' y ')'
    // haciendo -1 para ignorar el primer '('
    size_t largo_nombre = (size_t)(fin_nombre - inicio_nombre - 1);


    // evitamos excedernos del maximo de caracteres en char[256] comando, si no cabe el nombre entero se corta
    if (largo_nombre >= sizeof(info->comando)){
        largo_nombre = sizeof(info->comando) - 1;
    }

    // copia del nombre byte a byte por memcpy, agregando finalmente el '\0' para que sea un string valido en C
    memcpy(info->comando, inicio_nombre + 1, largo_nombre);
    info->comando[largo_nombre] = '\0';

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

    // string donde se almacena la ruta del /proc/<PID>/status
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
    strcpy(muestra->comando, stat.comando);
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


int ejecutar_pmon(pid_t pids[], size_t cantidad, unsigned int intervalo){
    
    if (pids == NULL || cantidad == 0) {
        return -1;
    }

    if (configurar_SIGALRM() == -1) {
        return -1;
    }

    // almacenamos el comportamiento anterior de SIGINT previo a la implementacion de pmon 
    struct sigaction sigint_anterior;

    if (configurar_SIGINT(&sigint_anterior) == -1) {
        return -1;
    }

    // inicializamos la flag para salir en 0
    salir_pmon = 0;

    MuestraProceso anteriores[cantidad];

    // for para tomar la primera muestra de cada proceso
    for (size_t i = 0; i < cantidad; i++) {

        // si falla la toma de muestras de alguno, marcamos la posicion como una muestra invalida como -1
        if (tomar_muestra(pids[i], &anteriores[i]) == -1) {
            anteriores[i].pid = -1;
        }
    }

    // tiempo inicial para calcular cuanto tiempo pasa hasta la siguiente muestra
    struct timespec tiempo_anterior;
    clock_gettime(CLOCK_MONOTONIC, &tiempo_anterior);

    // ciclo para repetir periodicamente la toma de muestras y actualizacion de procesos mostrados en pmon
    while (1) {

        // alarma para la siguiente actualizacion
        alarm(intervalo);

        // llamamos a funcion auxiliar para dejar suspendido pmon mientras llega SIGALRM o SIGINT
        esperar_actualizacion();

        // si se activa flag para salir alterada por SIGINT, se abandona el ciclo del pmon
        if(salir_pmon){
            break;
        }

        MuestraProceso actuales[cantidad];

        // for para tomar la muestra actual de cada proceso
        for (size_t i = 0; i < cantidad; i++) {

            // si el proceso habia desaparecido, lo marcamos como invalida en las actuales y continuamos
            if (anteriores[i].pid == -1) {
                actuales[i].pid = -1;
                continue;
            }

            // si la muestra anterior existia pero el proceso desaparecio mientras se esperaba el intervalo, se marca como invalida
            if (tomar_muestra(pids[i], &actuales[i]) == -1) {
                actuales[i].pid = -1;
            }
        }

        // tiempo en que fueron tomadas las muestras actuales
        struct timespec tiempo_actual;
        clock_gettime(CLOCK_MONOTONIC, &tiempo_actual);

        // calculamos cuanto tiempo paso entre ambas muestras en segundos
        double dif_tiempo = calcular_dif_tiempo(tiempo_anterior, tiempo_actual);

        mostrar_titulos();

        // calculamos y mostramos el %CPU de cada proceso valido
        for (size_t i = 0; i < cantidad; i++) {

            // si no es valido simplemente se salta
            if (actuales[i].pid == -1) {
                continue;
            }

            double cpu = calcular_cpu(anteriores[i].cpu_ticks,actuales[i].cpu_ticks,dif_tiempo);

            mostrar_fila(&actuales[i], cpu);
        }

        printf("\n");

        // las muestras actuales pasan a ser las anteriores para la siguiente actualizacion
        for (size_t i = 0; i < cantidad; i++) {
            anteriores[i] = actuales[i];
        }

        // el tiempo actual pasa a ser el anterior para la siguiente actualizacion
        tiempo_anterior = tiempo_actual;
    }

    // cancelamos cualquier alarma pendiente antes de salir de pmon por medio de CTRL + C
    alarm(0);

    // restauramos el comportamiento inicial de SIGINT previo a pmon
    sigaction(SIGINT, &sigint_anterior, NULL);

    return 0;
}