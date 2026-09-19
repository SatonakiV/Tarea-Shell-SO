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
 
 



void jobs_block_sigchld(sigset_t *old_mask) {
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_mask, old_mask);
}
 
void jobs_unblock_sigchld(const sigset_t *old_mask) {
    sigprocmask(SIG_SETMASK, old_mask, NULL);
}


 
static void free_job(Job *job) {
    free(job->pids);
    free(job->command_line);
    memset(job, 0, sizeof(*job));
    job->active = 0;
}
 


int jobs_add(const pid_t *pids, size_t pid_count, int background, const char *command_line){

    Job *slot = NULL;
 
    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jobs[i].active) {
            slot = &jobs[i];
            break;
        }
    }
 
    if (slot == NULL || pid_count == 0) { return -1;}
 
    pid_t *pids_copy = malloc(pid_count * sizeof(*pids_copy));
    char *line_copy = strdup(command_line != NULL ? command_line : "");
 
    if (pids_copy == NULL || line_copy == NULL) {
        free(pids_copy);
        free(line_copy);
        return -1;
    }
 
    memcpy(pids_copy, pids, pid_count * sizeof(*pids_copy));
 
    slot->id = next_job_id++;
    slot->active = 1;
    slot->pids = pids_copy;
    slot->pid_count = pid_count;
    slot->pending = pid_count;
    slot->state = JOB_RUNNING;
    slot->exit_code = 0;
    slot->background = background;
    slot->notified = 0;
    slot->command_line = line_copy;
 
    return slot->id;
}
 



int jobs_wait_foreground(int job_id) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);
 
    Job *job = find_job_by_id(job_id);
 


    if (job == NULL) {
        jobs_unblock_sigchld(&old_mask);
        return -1;
    }
 
    while (job->state != JOB_DONE) { sigsuspend(&old_mask);}
 
    int code = job->exit_code;
    free_job(job);
 
    jobs_unblock_sigchld(&old_mask);
 
    return code;
}





void jobs_list(void) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);
 
    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jobs[i].active || !jobs[i].background) {
            continue;
        }
 
        const char *estado = jobs[i].state == JOB_DONE ? "Terminado" : "Ejecutando";
        pid_t pid_mostrado = jobs[i].pids[jobs[i].pid_count - 1];
 
        printf("[%d] %d %s %s\n", jobs[i].id, pid_mostrado, estado,
               jobs[i].command_line);
    }
 
    jobs_unblock_sigchld(&old_mask);
}
 
void jobs_notify_done(void) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);
 

    
    int most_recent_done_id = -1;
    for (int i = 0; i < JOBS_MAX; i++) {
        if (jobs[i].active && jobs[i].background &&
            jobs[i].state == JOB_DONE && !jobs[i].notified &&
            jobs[i].id > most_recent_done_id) {
            most_recent_done_id = jobs[i].id;
        }
    }
 
    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jobs[i].active || !jobs[i].background) {
            continue;
        }
        if (jobs[i].state != JOB_DONE || jobs[i].notified) {
            continue;
        }
 
        char marca = (jobs[i].id == most_recent_done_id) ? '+' : '-';
        printf("[%d]%c Done %s\n", jobs[i].id, marca, jobs[i].command_line);
 
        free_job(&jobs[i]);
    }
 
    jobs_unblock_sigchld(&old_mask);
}