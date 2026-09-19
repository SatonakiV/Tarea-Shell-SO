#ifndef JOBS_H
#define JOBS_H
 
#include <signal.h>
#include <sys/types.h>
#include <stddef.h>
#define JOBS_MAX 64
 
void jobs_init(void);
 

int jobs_add(const pid_t *pids, size_t pid_count, int background,
             const char *command_line);

void jobs_block_sigchld(sigset_t *old_mask);
void jobs_unblock_sigchld(const sigset_t *old_mask);
 

int jobs_wait_foreground(int job_id);
 
void jobs_list(void);
 
void jobs_notify_done(void);
 
#endif
 