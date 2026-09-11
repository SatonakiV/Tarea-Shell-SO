#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "executor.h"

static Command wrap_argv(char **argv) {
    Command command = (Command){0};
    size_t argc = 0;

    while(argv[argc] != NULL) {
        argc++;
    }

    command.argv = argv;
    command.argc = argc;

    return command;
}

static void run_case(const char *title, char **argv, int expected) {
    Command command = wrap_argv(argv);
    Pipeline pipeline = (Pipeline){0};
    int status = -999;
    int rc;

    pipeline.commands = &command;
    pipeline.command_count = 1;

    printf("+++ %s +++ \n", title);
    fflush(stdout);

    rc = execute_pipeline(&pipeline, &status);

    printf("--> rc=%d status=%d (esperado %d) %s\n\n", rc, status, expected, (rc == 0 && status == expected) ? "OK" : "FALLO");
    fflush(stdout);
}

int main(void) {
    static char *echo_abs[] = { "/bin/echo", "hola", "mundo", NULL };
    static char *echo_path[] = { "echo", "busqueda", "por", "PATH", NULL };
    static char *falso[] = { "false", NULL };
    static char *inexistente[] = { "comando-que-no-existe-xyz", NULL };
    static char *un_dir[] = { "/etc", NULL };
    static char *suicida[] = { "./test_suicida", NULL };

    run_case("1. ruta absoluta", echo_abs, 0);
    run_case("2. busqueda en PATH", echo_path, 0);
    run_case("3. codigo de salida distinto de 0", falso, 1);
    run_case("4. execvp falla: no existe -> 127", inexistente, EXEC_NOT_FOUND);
    run_case("5. execvp falla: no ejecutable -> 126", un_dir, EXEC_CANNOT_EXECUTE);

    // Muere por SIGTERM (15): comprueba la rama WIFSIGNALED -> 128 + 15
    run_case("6. el hijo muere por senal -> 128+15", suicida, 143);

    // Linea vacia: pipeline sin comandos. No debe forkear ni tocar status
    {
        Pipeline vacio = (Pipeline){0};
        int status = -999;
        int rc = execute_pipeline(&vacio, &status);

        printf("=== 7. pipeline vacio ===\n");
        printf("--> rc=%d status=%d (no se toca) %s\n\n", rc, status, (rc == 0 && status == -999) ? "OK" : "FALLO");
    }

    printf("La shell sigue viva: pid=%d\n", (int)getpid());
    return 0;
}