#define _POSIX_C_SOURCE 200809L
 
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
 
#include "jobs.h"
 
typedef enum {
    JOB_RUNNING,
    JOB_DONE
} JobState;
 
typedef struct {
    int id;
    int active;                  
    pid_t *pids;                 
    size_t pid_count;
    size_t pending;               
    volatile sig_atomic_t state;  
    int exit_code;                
    int background;
    int notified;                 
    char *command_line;
} Job;
 
static Job jobs[JOBS_MAX];
static int next_job_id = 1;
 





static Job *find_job_by_pid(pid_t pid) {
    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jobs[i].active) {
            continue;
        }
        for (size_t j = 0; j < jobs[i].pid_count; j++) {
            if (jobs[i].pids[j] == pid) {
                return &jobs[i];
            }
        }
    }
    return NULL;
}
 
static Job *find_job_by_id(int id) {
    for (int i = 0; i < JOBS_MAX; i++) {
        if (jobs[i].active && jobs[i].id == id) {
            return &jobs[i];
        }
    }
    return NULL;
}
 
