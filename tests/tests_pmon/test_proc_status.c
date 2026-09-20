#include <stdio.h>
#include <stdlib.h>

#include "pmon.h"

int main(int argc, char *argv[]){
    if (argc != 2) {
        fprintf(stderr, "Uso correcto: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);

    unsigned long rss_kb;

    if (leer_proc_status(pid, &rss_kb) == -1) {
        fprintf(stderr, "Error, no se pudo leer VmRSS de /proc/%ld/status\n",
                (long)pid);
        return 1;
    }

    printf("PID: %ld\n", (long)pid);
    printf("VmRSS: %lu kB\n", rss_kb);

    return 0;
}