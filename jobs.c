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
 



/*                                       /;    ;\
                                   __  \\____//
                                  /{_\_/   `'\____
                                  \___   (o)  (o  }
       _____________________________/          :--'  
   ,-,'`@@@@@@@@       @@@@@@         \_    `__\
  ;:(  @@@@@@@@@        @@@             \___(o'o)
  :: )  @@@@          @@@@@@        ,'@@(  `===='       
  :: : @@@@@:          @@@@         `@@@:
  :: \  @@@@@:       @@@@@@@)    (  '@@@'
  ;; /\      /`,    @@@@@@@@@\   :@@@@@)
  ::/  )    {_----------------:  :~`,~~;
 ;;'`; :   )                  :  / `; ;
;;;; : :   ;                  :  ;  ; :              
`'`' / :  :                   :  :  : :
    )_ \__;      ";"          :_ ;  \_\       `,','
    :__\  \    * `,'*         \  \  :  \   *  8`;'*  *
        `^'     \ :/           `^'  `-^-'   \v/ :  \/ 
*/







static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno; //refresh
    int status;
    pid_t pid;
 

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Job *job = find_job_by_pid(pid);
 
        if (job == NULL) {
            continue;
        }
 
        int code;
        if (WIFEXITED(status)) {
            code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            code = 128 + WTERMSIG(status);
        } else {
            continue;
        }
 


        if (job->pending > 0) {
            job->pending--;
        }
 
        //(cmdN de cmd1|cmd2|...|cmdN-->ultimo de salida)
        if (pid == job->pids[job->pid_count - 1]) {
            job->exit_code = code;
        }
 
        if (job->pending == 0) {
            job->state = JOB_DONE; //  job listo!!
        }
    }
 
    errno = saved_errno;
}
 



void jobs_init(void) {
    memset(jobs, 0, sizeof(jobs));
 
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
 
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("mishell: sigaction(SIGCHLD)");
    }
}
 
 


