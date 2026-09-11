#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "executor.h"

static Command wrap_command(char **argv, Redirection *redirs, size_t redir_count) {
    Command command = (Command){0};
    size_t argc = 0;

    while (argv[argc] != NULL) {
        argc++;
    }

    command.argv = argv;
    command.argc = argc;
    command.redirs = redirs;
    command.redir_count = redir_count;

    return command;
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        perror(path);

        return;
    }

    fputs(content, file);
    fclose(file);
}

static void check_file(const char *path, const char *expected) {
    static char buffer[512];
    FILE *file = fopen(path, "r");
    size_t length;

    if (file == NULL) {
        printf("    [%s] %s no existe\n", expected == NULL ? "OK" : "FALLO", path);

        return;
    }

    length = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[length] = '\0';
    fclose(file);

    if (expected != NULL && strcmp(buffer, expected) == 0) {
        printf("    [OK] %s contiene lo esperado\n", path);
    } 
    else {
        printf("    [FALLO] %s contiene \"%s\", se esperaba \"%s\"\n", path, buffer, expected == NULL ? "(no existir)" : expected);
    }
}

static void run_redir_case(const char *title, char **argv, Redirection *redirs, size_t redir_count, int expected) {
    Command command = wrap_command(argv, redirs, redir_count);
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

static void run_case(const char *title, char **argv, int expected) {
    run_redir_case(title, argv, NULL, 0, expected);
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

    {
        static char *echo_hola[] = { "echo", "hola", NULL };
        static char *echo_otra[] = { "echo", "otra", NULL };
        static char *sort_cmd[] = { "sort", NULL };
        static char *cat_cmd[] = { "cat", NULL };
        static char *noexiste2[] = { "comando-que-no-existe-xyz", NULL };

        static Redirection out[] = { { REDIR_OUTPUT, "test_out.txt" } };
        static Redirection append[] = { { REDIR_APPEND, "test_out.txt" } };
        static Redirection in[] = { { REDIR_INPUT,  "test_in.txt"  } };
        static Redirection in_bad[] = { { REDIR_INPUT,  "/no/existe"   } };
        static Redirection in_out[] = { { REDIR_INPUT,  "test_in.txt"  }, { REDIR_OUTPUT, "test_out.txt" } };

        remove("test_out.txt");
        run_redir_case("8. > crea y escribe", echo_hola, out, 1, 0);
        check_file("test_out.txt", "hola\n");

        run_redir_case("9. >> acumula", echo_hola, append, 1, 0);
        check_file("test_out.txt", "hola\nhola\n");

        run_redir_case("10. > trunca", echo_otra, out, 1, 0);
        check_file("test_out.txt", "otra\n");

        write_file("test_in.txt", "pera\nmanzana\nuva\n");
        run_redir_case("11. < lee del archivo", cat_cmd, in, 1, 0);

        run_redir_case("12. < y > combinados", sort_cmd, in_out, 2, 0);
        check_file("test_out.txt", "manzana\npera\nuva\n");

        run_redir_case("13. open() falla -> _exit(1)", cat_cmd, in_bad, 1, 1);

        remove("test_out.txt");
        run_redir_case("14. execvp falla pero el open ya ocurrio", noexiste2, out, 1, EXEC_NOT_FOUND);
        check_file("test_out.txt", "");

        remove("test_out.txt");
        remove("test_in.txt");
    }

    printf("La shell sigue viva: pid=%d\n", (int)getpid());
    return 0;
}