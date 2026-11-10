#include "shell.h"
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/**
 * ====================================================================================
 * COMANDO: editor [archivo]
 * ====================================================================================
 * CATEGORÍA: edicion
 *
 * Justificación de la categoría (ver también el documento de sustentación):
 * El editor de texto EAFIT (carpeta ../editor/) es un programa POSIX completo con
 * su propio ciclo de vida: corre su propio bucle interactivo (prompt "cmd>") y
 * administra su propia memoria dinámica. NO es una función más que pueda
 * compartir el espacio de direcciones del shell (como sí hacen d_create o m_sbrk).
 * Igual que un shell real invoca a 'vi' o 'nano' como un proceso hijo separado, este
 * comando bifurca el shell (fork) y reemplaza la imagen de memoria del proceso hijo
 * (execvp) por el binario ya compilado del editor, y el shell padre espera (waitpid)
 * a que termine para recuperar el control del prompt.
 *
 * Parcial 2: el editor ahora comprime y descomprime en segundo plano con varios
 * hilos (comandos c/x/k/s) y usa Ctrl+C para cancelar esas tareas. Como el shell y
 * el editor comparten la terminal, Ctrl+C le llega a LOS DOS. Para que el shell no
 * muera mientras el editor está abierto, el shell IGNORA SIGINT y SIGQUIT durante
 * el waitpid (lo mismo que hacen bash o sh con sus procesos en primer plano) y el
 * hijo los devuelve a su comportamiento por defecto antes del execvp: una señal
 * ignorada se hereda a través de exec, y el editor necesita recibir SIGINT.
 *
 * Syscalls explicadas:
 * 1. sigaction(2): Ignora SIGINT/SIGQUIT en el shell mientras espera al editor, y
 *    restaura el manejo anterior al terminar.
 * 2. fork(2): Crea un proceso hijo idéntico al shell para no bloquear ni reemplazar
 *    el proceso del shell mismo mientras el editor está abierto.
 * 3. execvp(3): En el hijo, reemplaza su imagen de memoria por el binario del editor.
 * 4. waitpid(2): El shell padre se bloquea hasta que el editor termina ('q' en su
 *    modo comando) y entonces recupera el control del prompt "sys-shell>".
 */

/* Ruta del editor relativa al binario del shell (repo/shell/sys_shell ->
 * repo/editor/editor), así funciona sin importar desde qué carpeta se lance. */
static void ruta_del_editor(char *out, size_t cap) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *barra = strrchr(exe, '/');
        if (barra) {
            *barra = '\0';
            snprintf(out, cap, "%s/../editor/editor", exe);
            return;
        }
    }
    snprintf(out, cap, "../editor/editor");
}

int cmd_editor(int argc, char **argv) {
    char ruta_editor[PATH_MAX + 32];
    ruta_del_editor(ruta_editor, sizeof(ruta_editor));

    if (access(ruta_editor, X_OK) == -1) {
        fprintf(stderr, COLOR_ERROR
            "El binario del editor no existe o no es ejecutable: %s\n" COLOR_RESET
            COLOR_INFO "Compílalo primero con: make (desde la raíz del repositorio)\n" COLOR_RESET,
            ruta_editor);
        return 1;
    }

    /* argv para execvp: [ruta_editor, archivo (opcional), NULL] */
    char *editor_argv[3];
    editor_argv[0] = ruta_editor;
    editor_argv[1] = (argc >= 2) ? argv[1] : NULL;
    editor_argv[2] = NULL;

    /* 1. LLAMADA AL SISTEMA: sigaction — el shell ignora Ctrl+C mientras el editor corre */
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

    /* 2. LLAMADA AL SISTEMA: fork */
    fflush(stdout); /* que el hijo no herede texto pendiente del buffer */
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
        /* PROCESO HIJO: las señales vuelven a su comportamiento por defecto
         * (SIG_IGN se heredaría a través de execvp) y se carga el editor. */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        LOG_SYSCALL("execvp", "\"%s\", argv", ruta_editor);
        printf("\n");
        fflush(stdout);
        execvp(ruta_editor, editor_argv);

        /* Solo se llega aquí si execvp() falló */
        LOG_SYSCALL_ERROR(strerror(errno));
        fprintf(stderr, COLOR_ERROR "No se pudo iniciar el editor.\n" COLOR_RESET);
        _exit(127);
    }

    /* PROCESO PADRE (el shell): registra el hijo y espera a que termine */
    LOG_SYSCALL_RESULT(pid);
    printf(COLOR_INFO "Abriendo editor EAFIT (PID %d)...\n" COLOR_RESET, pid);
    fflush(stdout);

    int status;
    pid_t waited;
    /* 3. LLAMADA AL SISTEMA: waitpid (se reintenta si una señal la interrumpe) */
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);

    /* 4. Restaurar el manejo de señales que tenía el shell */
    sigaction(SIGINT, &viejo_int, NULL);
    sigaction(SIGQUIT, &viejo_quit, NULL);

    LOG_SYSCALL("waitpid", "%d, &status, 0", pid);
    if (waited == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(waited);

    if (WIFEXITED(status)) {
        printf(COLOR_RESULT "Editor cerrado. Código de salida: %d\n" COLOR_RESET, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf(COLOR_ERROR "El editor terminó de forma anormal por la señal %d\n" COLOR_RESET, WTERMSIG(status));
    }
    return 0;
}
