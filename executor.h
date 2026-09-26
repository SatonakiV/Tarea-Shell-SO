#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "shell.h"
#include <sys/types.h>

#define EXEC_CANNOT_EXECUTE 126
#define EXEC_NOT_FOUND 127

int execute_pipeline(const Pipeline *pipeline, const char *line, int *status);
void executor_set_foreground_pgid(pid_t pgid);
pid_t executor_get_foreground_pgid(void);

#endif
