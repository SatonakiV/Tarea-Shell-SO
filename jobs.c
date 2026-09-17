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
 
