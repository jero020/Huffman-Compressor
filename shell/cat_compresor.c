#include "shell.h"
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/**
 * ====================================================================================
 * CATEGORÍA: compresion (Parcial 2)
 * ====================================================================================
 * comprimir <archivo> [salida] [-t hilos]
 * descomprimir <archivo.huf> [salida] [-t hilos]
 *
 * Igual que 'editor', estos comandos no meten el compresor dentro del proceso del
 * shell: lanzan el programa ../huffcli como un proceso hijo (fork + execvp) y el
 * shell espera a que termine (waitpid). huffcli crea internamente su propio grupo de
 * hilos para comprimir en paralelo y muestra una barra de progreso.
 *
 * Ctrl+C: la terminal envía SIGINT al shell y a huffcli a la vez. Mientras huffcli
 * corre, el shell IGNORA SIGINT/SIGQUIT (sigaction con SIG_IGN), así la señal solo
 * cancela la compresión y el shell sigue vivo. El hijo restaura SIG_DFL antes del
 * execvp, porque una señal ignorada se hereda a través de exec y huffcli la necesita.
 *
 * Syscalls: sigaction(2), fork(2), execvp(3), waitpid(2)
 */

/* Ruta de huffcli relativa al binario del shell (repo/shell/sys_shell -> repo/huffcli). */
static void ruta_del_compresor(char *out, size_t cap) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *barra = strrchr(exe, '/');
        if (barra) {
            *barra = '\0';
            snprintf(out, cap, "%s/../huffcli", exe);
            return;
        }
    }
    snprintf(out, cap, "../huffcli");
}

/* Lanza huffcli con los argumentos dados y espera a que termine. */
static int ejecutar_compresor(char **args) {
    /* 1. Ignorar Ctrl+C en el shell mientras huffcli corre */
    struct sigaction ignorar, viejo_int, viejo_quit;
    memset(&ignorar, 0, sizeof(ignorar));
    ignorar.sa_handler = SIG_IGN;
    sigemptyset(&ignorar.sa_mask);
    LOG_SYSCALL("sigaction", "SIGINT/SIGQUIT, SIG_IGN");
    if (sigaction(SIGINT, &ignorar, &viejo_int) == -1 ||
        sigaction(SIGQUIT, &ignorar, &viejo_quit) == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(0);

    /* 2. fork */
    fflush(stdout);
    LOG_SYSCALL("fork", "");
    fflush(stdout); /* si no, el hijo hereda este texto en su buffer y lo imprime dos veces */
    pid_t pid = fork();
    if (pid == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        sigaction(SIGINT, &viejo_int, NULL);
        sigaction(SIGQUIT, &viejo_quit, NULL);
        return 1;
    }

    if (pid == 0) {
        /* HIJO: señales por defecto y reemplazar la imagen por huffcli */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        LOG_SYSCALL("execvp", "\"%s\", argv", args[0]);
        printf("\n");
        fflush(stdout);
        execvp(args[0], args);
        LOG_SYSCALL_ERROR(strerror(errno));
        fprintf(stderr, COLOR_ERROR "No se pudo ejecutar el compresor.\n" COLOR_RESET);
        _exit(127);
    }

    /* PADRE: esperar (reintentando si una señal interrumpe waitpid) */
    LOG_SYSCALL_RESULT(pid);
    fflush(stdout);
    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);

    /* 3. Restaurar el manejo de señales del shell */
    sigaction(SIGINT, &viejo_int, NULL);
    sigaction(SIGQUIT, &viejo_quit, NULL);

    LOG_SYSCALL("waitpid", "%d, &status, 0", pid);
    if (waited == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(waited);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0)        printf(COLOR_RESULT "Listo.\n" COLOR_RESET);
        else if (code == 130) printf(COLOR_INFO "Cancelado con Ctrl+C: no quedó ningún archivo a medias.\n" COLOR_RESET);
        else                  printf(COLOR_ERROR "El compresor terminó con error (código %d).\n" COLOR_RESET, code);
    } else if (WIFSIGNALED(status)) {
        printf(COLOR_ERROR "El compresor terminó por la señal %d\n" COLOR_RESET, WTERMSIG(status));
    }
    return 0;
}

/* Arma los argumentos de huffcli: <modo> <entrada> <salida> [-t N] */
static int comando_compresion(int argc, char **argv, const char *modo) {
    const char *uso = (strcmp(modo, "-c") == 0)
        ? "Uso: comprimir <archivo> [salida] [-t hilos]"
        : "Uso: descomprimir <archivo.huf> [salida] [-t hilos]";

    const char *entrada = NULL, *salida = NULL, *hilos = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, COLOR_ERROR "%s\n" COLOR_RESET, uso); return 1; }
            hilos = argv[++i];
        } else if (!entrada) {
            entrada = argv[i];
        } else if (!salida) {
            salida = argv[i];
        } else {
            fprintf(stderr, COLOR_ERROR "%s\n" COLOR_RESET, uso);
            return 1;
        }
    }
    if (!entrada) { fprintf(stderr, COLOR_ERROR "%s\n" COLOR_RESET, uso); return 1; }

    /* Salida por defecto: archivo.huf al comprimir; sin .huf + .restaurado al descomprimir */
    char salida_def[PATH_MAX];
    if (!salida) {
        size_t len = strlen(entrada);
        if (strcmp(modo, "-c") == 0)
            snprintf(salida_def, sizeof(salida_def), "%s.huf", entrada);
        else if (len > 4 && strcmp(entrada + len - 4, ".huf") == 0)
            snprintf(salida_def, sizeof(salida_def), "%.*s.restaurado", (int)(len - 4), entrada);
        else
            snprintf(salida_def, sizeof(salida_def), "%s.restaurado", entrada);
        salida = salida_def;
    }

    char ruta[PATH_MAX + 16];
    ruta_del_compresor(ruta, sizeof(ruta));
    if (access(ruta, X_OK) == -1) {
        fprintf(stderr, COLOR_ERROR "El compresor no existe o no es ejecutable: %s\n" COLOR_RESET
                COLOR_INFO "Compílalo primero con: make (desde la raíz del repositorio)\n" COLOR_RESET, ruta);
        return 1;
    }

    printf(COLOR_INFO "%s '%s' -> '%s'\n" COLOR_RESET,
           strcmp(modo, "-c") == 0 ? "Comprimiendo" : "Descomprimiendo", entrada, salida);

    char *args[7];
    int n = 0;
    args[n++] = ruta;
    args[n++] = (char *)modo;
    args[n++] = (char *)entrada;
    args[n++] = (char *)salida;
    if (hilos) { args[n++] = "-t"; args[n++] = (char *)hilos; }
    args[n] = NULL;
    return ejecutar_compresor(args);
}

int cmd_comprimir(int argc, char **argv)    { return comando_compresion(argc, argv, "-c"); }
int cmd_descomprimir(int argc, char **argv) { return comando_compresion(argc, argv, "-d"); }
