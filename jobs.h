#ifndef JOBS_H
#define JOBS_H
 
#include <signal.h>
#include <sys/types.h>
#include <stddef.h>
#define JOBS_MAX 64
 
void jobs_init(void);

// control de jobs (bonus Ctrl+Z): activo solo si la shell lee de una terminal y es su grupo foreground
int jobs_job_control(void);
int jobs_own_group(int background);
void jobs_give_terminal(pid_t pgid);
void jobs_take_terminal(void);

// fg [n] y bg [n]: devuelven el codigo de salida del comando interno
int jobs_fg(const char *arg);
int jobs_bg(const char *arg);


int jobs_add(const pid_t *pids, size_t pid_count, int background,
             const char *command_line);

void jobs_block_sigchld(sigset_t *old_mask);
void jobs_unblock_sigchld(const sigset_t *old_mask);
 

int jobs_wait_foreground(int job_id);
 
void jobs_list(void);
 
void jobs_notify_done(void);

int jobs_get_background_pids(pid_t **pids, size_t *count);
 
#endif
 