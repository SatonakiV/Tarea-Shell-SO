#ifndef PMON_H
#define PMON_H

#include <sys/types.h>

typedef struct {
    char state;
    unsigned long utime;
    unsigned long stime;
} ProcStat;

typedef struct {
    pid_t pid;
    char state;
    unsigned long cpu_ticks;
    unsigned long rss_kb;
} MuestraProceso;

int leer_proc_stat(pid_t pid, ProcStat *info);

int leer_proc_status(pid_t pid, unsigned long *rss_kb);

int tomar_muestra(pid_t pid, MuestraProceso *muestra);
#endif