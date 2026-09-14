#ifndef PMON_H
#define PMON_H

#include <sys/types.h>

typedef struct {
    char state;
    unsigned long utime;
    unsigned long stime;
} ProcStat;

int leer_proc_stat(pid_t pid, ProcStat *info);

#endif