#include <stdio.h>
#include <stdlib.h>

#include "pmon.h"

int main(int argc, char *argv[]){
    
    
    if(argc != 2){
        fprintf(stderr, "Uso: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);

    ProcStat info;

    // Test leer_proc_stat
    if(leer_proc_stat(pid, &info) == -1){
        fprintf(stderr, "No se pudo leer /proc/%ld/stat\n",(long)pid);
        return 1;
    }

    printf("PID: %ld\n", (long)pid);
    printf("Estado: %c\n", info.state);
    printf("utime: %lu\n", info.utime);
    printf("stime: %lu\n", info.stime);


    // Test leer_proc_status
    unsigned long rss_kb;

    if (leer_proc_status(pid, &rss_kb) == -1) {
        fprintf(stderr, "No se pudo leer VmRSS\n");
        return 1;
    }

    printf("VmRSS: %lu kB\n", rss_kb);

    return 0;


}