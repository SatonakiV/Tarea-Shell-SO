#define _POSIX_C_SOURCE 200809L
 
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "jobs.h"

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
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
    int term_signal;              // senal que mato al ultimo comando (0 si termino con exit)
    int background;
    int notified;
    char *command_line;
    pid_t pgid;                   // grupo propio del job (0 si comparte el de la shell)
    int stop_signal;              // senal que lo detuvo (SIGTSTP con Ctrl+Z)
    int stop_notified;            // ya se aviso "Stopped" desde la ultima vez que se detuvo
    struct termios tmodes;        // modos de terminal que tenia al detenerse, para devolverselos con fg
    int has_tmodes;
} Job;

static Job jobs[JOBS_MAX];

// terminal de la shell: -1 si no hay control de jobs (stdin no es una terminal)
static int shell_terminal = -1;
static pid_t shell_pgid;
static struct termios shell_tmodes;





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

// como bash: el numero de un job nuevo es el mayor en uso + 1. Los foreground se liberan apenas
// terminan, asi que no gastan numeros y los ids vuelven a partir de 1 cuando no quedan jobs
static int new_job_id(void) {
    int max_id = 0;
    for (int i = 0; i < JOBS_MAX; i++) {
        if (jobs[i].active && jobs[i].id > max_id) {
            max_id = jobs[i].id;
        }
    }
    return max_id + 1;
}
 

static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno; //refresh
    int status;
    pid_t pid;
 

    // WUNTRACED y WCONTINUED: ademas de los hijos que terminan, avisan los que se detienen
    // (Ctrl+Z, o un background que quiso leer la terminal) y los que reanuda un SIGCONT
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *job = find_job_by_pid(pid);

        if (job == NULL) {
            continue;
        }

        if (WIFSTOPPED(status)) {
            job->stop_signal = WSTOPSIG(status);
            job->state = JOB_STOPPED;
            continue;
        }

        // fg y bg ya marcan el job antes de mandar SIGCONT; esto cubre un SIGCONT externo
        if (WIFCONTINUED(status)) {
            if (job->state == JOB_STOPPED) {
                job->state = JOB_RUNNING;
                job->stop_notified = 0;
            }
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
            job->term_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
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
    // SA_RESTART: que un hijo termine no debe cortar las llamadas de la shell. Asi getline() no
    // vuelve con EINTR (ni con media linea) mientras se espera un comando, y el aviso Done sale
    // con el proximo prompt. La espera del foreground igual despierta: sigsuspend() nunca se reinicia
    sa.sa_flags = SA_RESTART;
 
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("mishell: sigaction(SIGCHLD)");
    }

    // Control de jobs solo si la shell lee de una terminal y es su grupo foreground. Sin terminal
    // (por ejemplo un script por stdin) el foreground se queda en el grupo de la shell
    if (isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) == getpgrp()) {
        shell_terminal = STDIN_FILENO;
        shell_pgid = getpgrp();
        tcgetattr(shell_terminal, &shell_tmodes);

        // Ctrl+Z no debe detener a la shell, y tcsetpgrp() desde background (para recuperar la
        // terminal) manda SIGTTOU si no se ignora. Los hijos las vuelven a SIG_DFL antes de execvp()
        struct sigaction ignore;
        memset(&ignore, 0, sizeof(ignore));
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        sigaction(SIGTSTP, &ignore, NULL);
        sigaction(SIGTTIN, &ignore, NULL);
        sigaction(SIGTTOU, &ignore, NULL);
    }
}

int jobs_job_control(void) {
    return shell_terminal >= 0;
}

// Con control de jobs cada pipeline va en su propio grupo; sin terminal, solo los background
int jobs_own_group(int background) {
    return background || jobs_job_control();
}

// Le entrega la terminal a un grupo. La llaman el padre y el hijo, para que no importe quien corre
// primero despues del fork()
void jobs_give_terminal(pid_t pgid) {
    if (shell_terminal >= 0) {
        tcsetpgrp(shell_terminal, pgid);
    }
}

// La shell recupera la terminal y sus modos: un programa detenido pudo dejarla en modo raw
void jobs_take_terminal(void) {
    if (shell_terminal >= 0) {
        tcsetpgrp(shell_terminal, shell_pgid);
        tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
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

    // Eliminar el \n que getline() deja al final de la linea y los espacios sobrantes. En background
    // tambien el '&' final (el parser garantiza que es lo ultimo): jobs y Done muestran "sleep 30"
    if (line_copy != NULL) {
        size_t len = strlen(line_copy);
        while (len > 0 && isspace((unsigned char)line_copy[len - 1])) {
            line_copy[--len] = '\0';
        }
        if (background && len > 0 && line_copy[len - 1] == '&') {
            line_copy[--len] = '\0';
            while (len > 0 && isspace((unsigned char)line_copy[len - 1])) {
                line_copy[--len] = '\0';
            }
        }
    }
 
    if (pids_copy == NULL || line_copy == NULL) {
        free(pids_copy);
        free(line_copy);
        return -1;
    }
 
    memcpy(pids_copy, pids, pid_count * sizeof(*pids_copy));
 
    slot->id = new_job_id();
    slot->active = 1;
    slot->pids = pids_copy;
    slot->pid_count = pid_count;
    slot->pending = pid_count;
    slot->state = JOB_RUNNING;
    slot->exit_code = 0;
    slot->term_signal = 0;
    slot->background = background;
    slot->notified = 0;
    slot->command_line = line_copy;
    // el primer hijo es el lider del grupo (ver execute_pipeline)
    slot->pgid = jobs_own_group(background) ? pids[0] : 0;
    slot->stop_signal = 0;
    slot->stop_notified = 0;
    slot->has_tmodes = 0;
 
    return slot->id;
}
 



int jobs_wait_foreground(int job_id) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);
 
    Job *job = find_job_by_id(job_id);



    if (job == NULL) {
        jobs_take_terminal();
        jobs_unblock_sigchld(&old_mask);
        return -1;
    }

    // despierta cuando el job termina (JOB_DONE) o cuando Ctrl+Z lo detiene (JOB_STOPPED)
    while (job->state == JOB_RUNNING) { sigsuspend(&old_mask);}

    int stopped = job->state == JOB_STOPPED;
    int code;

    // la terminal hizo eco de "^C", "^\" o "^Z" sin salto de linea: se cierra la linea para que el
    // aviso o el prompt no queden pegados
    if (jobs_job_control() &&
        (stopped || job->term_signal == SIGINT || job->term_signal == SIGQUIT)) {
        putchar('\n');
    }

    if (stopped) {
        // Ctrl+Z: el job queda detenido en background (el aviso "Stopped" sale con el proximo
        // prompt) y se guardan sus modos de terminal para devolverselos con fg
        if (shell_terminal >= 0) {
            job->has_tmodes = tcgetattr(shell_terminal, &job->tmodes) == 0;
        }
        job->background = 1;
        job->stop_notified = 0;
        code = 128 + job->stop_signal;
    } else {
        code = job->exit_code;
        free_job(job);
    }

    jobs_take_terminal();
    jobs_unblock_sigchld(&old_mask);

    return code;
}

// Manda una senal a todo el job: a su grupo, o PID por PID si comparte el grupo de la shell
static void signal_job(const Job *job, int sig) {
    if (job->pgid > 0) {
        kill(-job->pgid, sig);
        return;
    }

    for (size_t j = 0; j < job->pid_count; j++) {
        kill(job->pids[j], sig);
    }
}

// Elige el job de fg/bg: el indicado ("2" o "%2") o, sin argumento, el mas reciente. bg solo acepta
// jobs detenidos. Se llama con SIGCHLD bloqueado
static Job *select_job(const char *arg, int only_stopped, const char *name) {
    Job *job = NULL;

    if (arg == NULL) {
        for (int i = 0; i < JOBS_MAX; i++) {
            if (jobs[i].active && jobs[i].background && jobs[i].state != JOB_DONE &&
                (!only_stopped || jobs[i].state == JOB_STOPPED) &&
                (job == NULL || jobs[i].id > job->id)) {
                job = &jobs[i];
            }
        }

        if (job == NULL) {
            fprintf(stderr, "%s: no hay jobs%s\n", name, only_stopped ? " detenidos" : "");
        }
        return job;
    }

    const char *digits = arg[0] == '%' ? arg + 1 : arg;
    char *end = NULL;
    errno = 0;
    long id = strtol(digits, &end, 10);

    if (errno == 0 && end != digits && *end == '\0' && id > 0 && id <= INT_MAX) {
        job = find_job_by_id((int)id);
    }

    if (job == NULL || !job->background || job->state == JOB_DONE) {
        fprintf(stderr, "%s: %s: no existe ese job\n", name, arg);
        return NULL;
    }

    if (only_stopped && job->state != JOB_STOPPED) {
        fprintf(stderr, "%s: el job %d ya esta corriendo en background\n", name, job->id);
        return NULL;
    }

    return job;
}

// fg [n]: devuelve un job detenido o en background al foreground y lo espera
int jobs_fg(const char *arg) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);

    Job *job = select_job(arg, 0, "fg");

    if (job == NULL) {
        jobs_unblock_sigchld(&old_mask);
        return 1;
    }

    printf("%s\n", job->command_line);
    fflush(stdout);

    job->background = 0;
    job->stop_notified = 0;

    // primero la terminal (con los modos que tenia al detenerse) y despues SIGCONT: si siguiera
    // antes de tener la terminal, un programa que la lee se volveria a detener con SIGTTIN
    if (job->pgid > 0) {
        jobs_give_terminal(job->pgid);
    }
    if (shell_terminal >= 0 && job->has_tmodes) {
        tcsetattr(shell_terminal, TCSADRAIN, &job->tmodes);
    }
    if (job->state == JOB_STOPPED) {
        job->state = JOB_RUNNING;
        signal_job(job, SIGCONT);
    }

    int id = job->id;
    jobs_unblock_sigchld(&old_mask);

    // jobs_wait_foreground() espera, recupera la terminal y libera el job si termino
    return jobs_wait_foreground(id);
}

// bg [n]: reanuda en background un job detenido, sin darle la terminal
int jobs_bg(const char *arg) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);

    Job *job = select_job(arg, 1, "bg");

    if (job == NULL) {
        jobs_unblock_sigchld(&old_mask);
        return 1;
    }

    job->state = JOB_RUNNING;
    job->stop_notified = 0;
    signal_job(job, SIGCONT);
    printf("[%d]+ %s &\n", job->id, job->command_line);

    jobs_unblock_sigchld(&old_mask);
    return 0;
}





void jobs_list(void) {
    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);
 
    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jobs[i].active || !jobs[i].background) {
            continue;
        }
 
        const char *estado = jobs[i].state == JOB_DONE ? "Terminado" :
                             jobs[i].state == JOB_STOPPED ? "Detenido" : "Ejecutando";
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
            ((jobs[i].state == JOB_DONE && !jobs[i].notified) ||
             (jobs[i].state == JOB_STOPPED && !jobs[i].stop_notified)) &&
            jobs[i].id > most_recent_done_id) {
            most_recent_done_id = jobs[i].id;
        }
    }

    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jobs[i].active || !jobs[i].background) {
            continue;
        }

        char marca = (jobs[i].id == most_recent_done_id) ? '+' : '-';

        // un job detenido (Ctrl+Z, o un background que quiso leer la terminal) se avisa una sola vez
        // y sigue en la tabla hasta que fg o bg lo reanuden
        if (jobs[i].state == JOB_STOPPED && !jobs[i].stop_notified) {
            printf("[%d]%c Stopped %s\n", jobs[i].id, marca, jobs[i].command_line);
            jobs[i].stop_notified = 1;
            continue;
        }

        if (jobs[i].state != JOB_DONE || jobs[i].notified) {
            continue;
        }

        // un job que murio por una senal no termino "Done": se muestra la senal, como hace bash
        // ("Killed", "Terminated", ...)
        if (jobs[i].term_signal != 0) {
            printf("[%d]%c %s %s\n", jobs[i].id, marca, strsignal(jobs[i].term_signal),
                   jobs[i].command_line);
        } else {
            printf("[%d]%c Done %s\n", jobs[i].id, marca, jobs[i].command_line);
        }
 
        free_job(&jobs[i]);
    }
 
    jobs_unblock_sigchld(&old_mask);
}

// funcion para entregarle a pmon una lista con los PID de los jobs background activos que se deben monitorear
// (corriendo o detenidos: pmon muestra estos ultimos con estado T)
int jobs_get_background_pids(pid_t **pids, size_t *count){

    if (pids == NULL || count == NULL) {
        return -1;
    }

    *pids = NULL;
    *count = 0;

    sigset_t old_mask;
    jobs_block_sigchld(&old_mask);

    size_t total = 0;

    // contamos cuantos PID pertenecen a jobs background activos 
    for (int i = 0; i < JOBS_MAX; i++) {

        if (!jobs[i].active || !jobs[i].background || jobs[i].state == JOB_DONE) {
            continue;
        }

        total += jobs[i].pid_count;
    }

    // si no hay procesos activos, no hay nada que copiar
    if (total == 0) {
        jobs_unblock_sigchld(&old_mask);
        return 0;
    }

    pid_t *copy = malloc(total * sizeof(*copy));

    if (copy == NULL) {
        jobs_unblock_sigchld(&old_mask);
        return -1;
    }

    size_t position = 0;

    // copiamos los PID de todos los jobs background activos
    for (int i = 0; i < JOBS_MAX; i++) {

        if (!jobs[i].active || !jobs[i].background || jobs[i].state == JOB_DONE) {
            continue;
        }

        for (size_t j = 0; j < jobs[i].pid_count; j++) {
            copy[position] = jobs[i].pids[j];
            position++;
        }
    }

    *pids = copy;
    *count = position;

    jobs_unblock_sigchld(&old_mask);

    return 0;
}
