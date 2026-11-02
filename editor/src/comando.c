#include "comando.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  // Para printf
#include <fcntl.h>  // Para open() con O_WRONLY | O_CREAT al crear un archivo nuevo vacio
#include <errno.h>  // Para errno/strerror al iniciar tareas
#include <poll.h>   // Parcial 2: esperar entrada/señales sin espera activa
#include <signal.h> // Parcial 2: bloqueo de SIGINT/SIGTERM
#include <sys/signalfd.h> // Parcial 2: señales como descriptor dentro de poll()
#include <sys/stat.h> // Parcial 2: comparar si dos rutas son el mismo archivo

// ============================================================================
// MODO COMANDO
//  Parcial 1: o / p / a / d / q  -- operan sobre la lista ligada de lineas y
//             persisten a disco con open/read/write/close.
//  Parcial 2: c / x / k / s      -- compresion y descompresion Huffman
//             concurrente en SEGUNDO PLANO (ver hf_concurrent.h).
// ============================================================================

static int g_signal_fd = -1;

/** Barra de progreso de texto, p. ej. "[########............]". */
static void barra_progreso(char *out, int ancho, int pct) {
    int llenos = pct * ancho / 100;
    out[0] = '[';
    for (int i = 0; i < ancho; i++) out[1 + i] = (i < llenos) ? '#' : '.';
    out[ancho + 1] = ']';
    out[ancho + 2] = '\0';
}

static const char *nombre_op(hf_op_t op) {
    return op == HF_OP_COMPRESS ? "compresion" : "descompresion";
}

/** ¿Las dos rutas apuntan al mismo archivo? (mismo nombre o mismo inodo) */
static int mismo_archivo(const char *a, const char *b) {
    if (strcmp(a, b) == 0) return 1;
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/**
 * Termina la ejecucion del programa de forma segura: cancela y une la tarea
 * en segundo plano (si la hay), escribe cualquier guardado pendiente, libera
 * toda la memoria dinamica de la sesion (sin fugas) y sale con exit(0).
 */
static void comando_salir(EstadoEditor *e) {
    if (e->tarea) printf("Cancelando la tarea en segundo plano antes de salir...\n");
    editor_liberar(e);  /* cancela + join + guardado pendiente + free */
    if (g_signal_fd != -1) close(g_signal_fd);
    printf("Saliendo del editor. Memoria liberada correctamente.\n");
    exit(0);
}

/**
 * o [archivo]: abre/crea el archivo indicado y lo carga como el documento
 * activo de la sesion. Si open(O_RDONLY) falla porque el archivo no existe,
 * se crea vacio en disco de inmediato (open con O_CREAT) para dejarlo listo.
 */
static void modo_comando_o(EstadoEditor *e, const char *arg) {
    if (!arg || arg[0] == '\0') {
        printf("Uso: o <archivo>\n");
        return;
    }
    /* Parcial 2: el guard protege el archivo ACTUAL; cambiar de documento con
     * una tarea en curso dejaría cambios pendientes sin dueño. */
    if (e->tarea) {
        printf("Hay una %s en segundo plano: espere a que termine o cancelela con 'k' antes de abrir otro archivo.\n",
               nombre_op(e->tarea_op));
        return;
    }

    estructura_destruir(e->estructura);
    e->estructura = estructura_crear();

    int res = estructura_cargar_archivo(e->estructura, arg, NULL);

    free(e->nombreArchivo);
    e->nombreArchivo = malloc(strlen(arg) + 1);
    strcpy(e->nombreArchivo, arg);

    if (res == -1) {
        /* open(O_RDONLY) fallo: el archivo no existia. Lo creamos en disco
         * ahora mismo con open(O_WRONLY|O_CREAT, 0644), tal como pide 'o'.
         * NO usamos estructura_guardar_archivo() aqui: esa funcion escribiria
         * el '\n' de la linea en blanco inicial del documento en memoria,
         * dejando 1 byte en vez de un archivo realmente vacio (0 bytes). */
        int fd_nuevo = open(arg, O_WRONLY | O_CREAT, 0644);
        if (fd_nuevo != -1) close(fd_nuevo);
        printf("Archivo '%s' no existia: se creo vacio en disco.\n", arg);
    } else {
        printf("Archivo '%s' abierto (%d linea(s)).\n", arg, e->estructura->totalLineas);
    }
}

/**
 * p [n]: sin argumento, imprime todo el archivo linea por linea a FD 1
 * (stdout) con write(); con n, valida 1<=n<=totalLineas e imprime solo esa
 * linea. Lee la lista en memoria, asi que funciona durante una compresion.
 */
static void modo_comando_p(EstadoEditor *e, const char *arg) {
    char buffer[4096];
    fflush(stdout); /* printf y write() comparten la terminal: vaciar antes */
    if (!arg || arg[0] == '\0') {
        if (e->estructura->totalLineas == 0) {
            const char *msg = "(archivo vacio)\n";
            write(STDOUT_FILENO, msg, strlen(msg));
            return;
        }
        int n = 1;
        for (NodoLinea *actual = e->estructura->cabeza; actual; actual = actual->siguiente, n++) {
            char num[16];
            int nlen = snprintf(num, sizeof(num), "%3d: ", n);
            estructura_texto_de_linea(actual, buffer);
            write(STDOUT_FILENO, num, nlen);
            write(STDOUT_FILENO, buffer, strlen(buffer));
            write(STDOUT_FILENO, "\n", 1);
        }
    } else {
        char *fin = NULL;
        long n = strtol(arg, &fin, 10);
        if (*fin != '\0' || n <= 0 || n > e->estructura->totalLineas) {
            printf("Error: la linea '%s' no existe (el archivo tiene %d linea(s)).\n", arg, e->estructura->totalLineas);
            return;
        }
        NodoLinea *actual = e->estructura->cabeza;
        for (long i = 1; i < n && actual; i++) actual = actual->siguiente;
        estructura_texto_de_linea(actual, buffer);
        write(STDOUT_FILENO, buffer, strlen(buffer));
        write(STDOUT_FILENO, "\n", 1);
    }
}

/** Mensaje comun cuando un guardado quedo diferido por una tarea en curso. */
static void avisar_guardado_diferido(EstadoEditor *e) {
    printf("  El archivo esta siendo leido por la %s en segundo plano: el cambio queda en memoria\n"
           "  y el guardado en disco queda PENDIENTE hasta que la tarea termine.\n",
           nombre_op(e->tarea_op));
}

/**
 * a [texto]: anade el texto recibido como una nueva linea al final del
 * documento y persiste el archivo completo en disco (o difiere el guardado
 * si una compresion esta leyendo el archivo).
 */
static void modo_comando_a(EstadoEditor *e, const char *arg) {
    if (!arg || arg[0] == '\0') {
        printf("Uso: a <texto>\n");
        return;
    }

    NodoLinea *nueva = estructura_insertar_linea(e->estructura, e->estructura->cola);
    estructura_fijar_texto(nueva, arg);

    int res = editor_guardar(e);
    if (res == -1) {
        printf("Error al escribir en '%s'.\n", e->nombreArchivo);
    } else {
        printf("Linea agregada al final de '%s' (ahora %d linea(s)).\n", e->nombreArchivo, e->estructura->totalLineas);
        if (res == 1) avisar_guardado_diferido(e);
    }
}

/**
 * d [n]: borra la linea n (1..totalLineas), desplazando las lineas
 * posteriores, y persiste el archivo mas corto en disco (o difiere el
 * guardado). Si el archivo queda sin lineas, se guarda PRIMERO (deja el
 * archivo en 0 bytes exactos) y solo despues se reinserta una linea en
 * blanco para que el documento siempre tenga al menos un renglon en memoria.
 */
static void modo_comando_d(EstadoEditor *e, const char *arg) {
    char *fin = NULL;
    long n = (arg && arg[0] != '\0') ? strtol(arg, &fin, 10) : 0;
    int arg_invalido = (!arg || arg[0] == '\0' || *fin != '\0');

    if (arg_invalido || n <= 0 || n > e->estructura->totalLineas) {
        printf("Uso: d <n>  (linea entre 1 y %d)\n", e->estructura->totalLineas);
        return;
    }

    NodoLinea *actual = e->estructura->cabeza;
    for (long i = 1; i < n && actual; i++) actual = actual->siguiente;
    estructura_eliminar_linea(e->estructura, actual);

    /* Se guarda ANTES de reinsertar la linea de relleno, para que un
     * documento vacio quede en disco con tamano 0 exacto. */
    int res = editor_guardar(e);

    if (e->estructura->totalLineas == 0) {
        estructura_insertar_linea(e->estructura, NULL);
    }

    if (res == -1) {
        printf("Error al escribir en '%s'.\n", e->nombreArchivo);
    } else {
        printf("Linea %ld borrada de '%s' (quedan %d linea(s)).\n", n, e->nombreArchivo, e->estructura->totalLineas);
        if (res == 1) avisar_guardado_diferido(e);
    }
}

/* ======================= Parcial 2: comandos nuevos ====================== */

/**
 * c [salida]: comprime el documento actual en segundo plano (por defecto a
 * '<archivo>.huf'). La UI sigue respondiendo mientras tanto.
 */
static void modo_comando_c(EstadoEditor *e, const char *arg) {
    if (e->tarea) {
        printf("Ya hay una %s en segundo plano (use 's' para ver su estado o 'k' para cancelarla).\n",
               nombre_op(e->tarea_op));
        return;
    }
    char salida[1024];
    if (arg && arg[0] != '\0') snprintf(salida, sizeof salida, "%s", arg);
    else                       snprintf(salida, sizeof salida, "%s.huf", e->nombreArchivo);

    if (mismo_archivo(salida, e->nombreArchivo)) {
        printf("Error: la salida no puede ser el mismo documento que se esta comprimiendo.\n");
        return;
    }
    /* Comprimir la ultima version: si quedo algo sin guardar, guardarlo ya. */
    if (e->guardado_pendiente && editor_guardar(e) != 0) {
        printf("Error: no se pudo guardar el documento antes de comprimir.\n");
        return;
    }

    /* El guard se toma como LECTOR dentro de hf_job_start (en este hilo), asi
     * no hay ventana entre "inicie" y "el editor escribio encima". */
    e->tarea = hf_job_start(HF_OP_COMPRESS, e->nombreArchivo, salida, 0, 0, &e->guard);
    if (!e->tarea) {
        printf("No se pudo iniciar la compresion: %s\n", strerror(errno));
        return;
    }
    e->tarea_op = HF_OP_COMPRESS;
    e->ultimo_decil = -1;
    printf("Comprimiendo '%s' -> '%s' en segundo plano.\n"
           "Puede seguir usando el editor ('s' = estado, 'k' = cancelar).\n", e->nombreArchivo, salida);
}

/**
 * x <archivo.huf> [salida]: descomprime en segundo plano. Por defecto la
 * salida es '<archivo sin .huf>.restaurado'. No se permite escribir sobre el
 * documento abierto (se perderian los cambios en memoria): se descomprime a
 * otro archivo y luego se abre con 'o'.
 */
static void modo_comando_x(EstadoEditor *e, char *arg) {
    if (e->tarea) {
        printf("Ya hay una %s en segundo plano (use 's' para ver su estado o 'k' para cancelarla).\n",
               nombre_op(e->tarea_op));
        return;
    }
    char *guarda = NULL;
    char *entrada = arg ? strtok_r(arg, " \t", &guarda) : NULL;
    char *salida_arg = entrada ? strtok_r(NULL, " \t", &guarda) : NULL;
    if (!entrada) {
        printf("Uso: x <archivo.huf> [salida]\n");
        return;
    }

    char salida[1024];
    if (salida_arg) {
        snprintf(salida, sizeof salida, "%s", salida_arg);
    } else {
        size_t len = strlen(entrada);
        if (len > 4 && strcmp(entrada + len - 4, ".huf") == 0)
            snprintf(salida, sizeof salida, "%.*s.restaurado", (int)(len - 4), entrada);
        else
            snprintf(salida, sizeof salida, "%s.restaurado", entrada);
    }

    if (mismo_archivo(salida, e->nombreArchivo)) {
        printf("Error: no se puede descomprimir sobre el documento abierto ('%s').\n"
               "Use otra salida y luego abrala con 'o'.\n", e->nombreArchivo);
        return;
    }
    if (mismo_archivo(salida, entrada)) {
        printf("Error: la entrada y la salida son el mismo archivo.\n");
        return;
    }

    e->tarea = hf_job_start(HF_OP_DECOMPRESS, entrada, salida, 0, 0, NULL);
    if (!e->tarea) {
        printf("No se pudo iniciar la descompresion: %s\n", strerror(errno));
        return;
    }
    e->tarea_op = HF_OP_DECOMPRESS;
    e->ultimo_decil = -1;
    printf("Descomprimiendo '%s' -> '%s' en segundo plano.\n", entrada, salida);
}

/** k: solicita la cancelacion de la tarea en segundo plano. */
static void modo_comando_k(EstadoEditor *e) {
    if (!e->tarea) { printf("No hay ninguna tarea en segundo plano.\n"); return; }
    hf_job_cancel(e->tarea);
    printf("Cancelando la %s...\n", nombre_op(e->tarea_op));
}

/** s: muestra el estado detallado de la tarea en segundo plano. */
static void modo_comando_s(EstadoEditor *e) {
    if (!e->tarea) {
        printf("No hay ninguna tarea en segundo plano.%s\n",
               e->guardado_pendiente ? " (hay un guardado pendiente)" : "");
        return;
    }
    hf_status_t st;
    hf_job_get_status(e->tarea, &st);
    char barra[64];
    barra_progreso(barra, 30, st.percent);
    printf("  %s %s %3d%%  fase: %s  bloques: %llu/%llu%s\n", nombre_op(e->tarea_op), barra,
           st.percent, st.phase, (unsigned long long)st.done, (unsigned long long)st.total,
           e->guardado_pendiente ? "  (guardado pendiente)" : "");
}

/**
 * Interpreta una linea cruda del "cmd>" (p. ej. "a Hola mundo" o "d 3") y
 * despacha al manejador correspondiente. El primer caracter no-espacio es
 * el nombre del comando; el resto (sin espacios iniciales ni el salto de
 * linea final) es su argumento.
 */
static void modo_comando_ejecutar(EstadoEditor *e, char *linea) {
    while (*linea == ' ') linea++;
    if (*linea == '\0') return;

    char cmd = linea[0];
    char *resto = linea + 1;
    while (*resto == ' ') resto++;

    size_t len = strlen(resto);
    while (len > 0 && (resto[len - 1] == '\n' || resto[len - 1] == '\r')) resto[--len] = '\0';

    switch (cmd) {
        case 'o': modo_comando_o(e, resto); break;
        case 'p': modo_comando_p(e, resto); break;
        case 'a': modo_comando_a(e, resto); break;
        case 'd': modo_comando_d(e, resto); break;
        case 'c': modo_comando_c(e, resto); break;
        case 'x': modo_comando_x(e, resto); break;
        case 'k': modo_comando_k(e); break;
        case 's': modo_comando_s(e); break;
        case 'q': comando_salir(e); break; /* No retorna */
        default:
            printf("Comando desconocido: '%c'. Los comandos validos son o, p, a, d, q, c, x, k, s.\n", cmd);
    }
}

/* El prompt muestra el progreso de la tarea, p. ej. "[c 45%] cmd> ". */
static void imprimir_prompt(EstadoEditor *e) {
    if (e->tarea) {
        hf_status_t st;
        hf_job_get_status(e->tarea, &st);
        printf("[%c %d%%] cmd> ", e->tarea_op == HF_OP_COMPRESS ? 'c' : 'x', st.percent);
    } else {
        printf("cmd> ");
    }
    fflush(stdout);
}

/**
 * Revisa la tarea en segundo plano SIN bloquear: muestra el progreso cada
 * 10% y, si termino, hace join e informa el resultado. Retorna 1 si imprimio
 * algo (para volver a dibujar el prompt).
 */
static int revisar_tarea(EstadoEditor *e) {
    if (!e->tarea) return 0;
    hf_status_t st;
    hf_job_get_status(e->tarea, &st);

    if (st.state == HF_STATE_RUNNING) {
        int decil = st.percent / 10;
        if (decil <= e->ultimo_decil) return 0;
        e->ultimo_decil = decil;
        char barra[64];
        barra_progreso(barra, 30, st.percent);
        printf("\n  [%s] %s %3d%%  (%s)\n", nombre_op(e->tarea_op), barra, st.percent, st.phase);
        return 1;
    }
    printf("\n");
    editor_terminar_tarea(e, 0);   /* join + resultado + guardado pendiente */
    return 1;
}

void comando_bucle_principal(EstadoEditor *e) {
    /* Señales: se BLOQUEAN antes de crear cualquier hilo (la mascara se
     * hereda) y se leen como un descriptor mas dentro de poll(). Asi una
     * señal nunca interrumpe a un hilo con un mutex tomado. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    g_signal_fd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (g_signal_fd == -1) perror("signalfd");

    printf("Editor EAFIT - modo comando (o/p/a/d/q) + Huffman en segundo plano (c/x/k/s)\n");
    printf("Archivo actual: %s\n", e->nombreArchivo);
    imprimir_prompt(e);

    char entrada[4096];
    size_t usados = 0;
    int stdin_abierto = 1;
    int descartando = 0;   /* ignorando el resto de una linea demasiado larga */

    while (1) {
        if (revisar_tarea(e)) imprimir_prompt(e);
        if (!stdin_abierto && !e->tarea) break;   /* EOF y nada pendiente */

        struct pollfd pfd[2] = {
            { stdin_abierto ? STDIN_FILENO : -1, POLLIN, 0 },
            { g_signal_fd, POLLIN, 0 },
        };
        /* Sin tarea: dormir hasta que llegue algo. Con tarea: despertar cada
         * 100 ms para refrescar el progreso. Nunca hay espera activa. */
        int r = poll(pfd, 2, e->tarea ? 100 : -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (r == 0) continue;

        if (g_signal_fd != -1 && (pfd[1].revents & POLLIN)) {
            struct signalfd_siginfo si;
            if (read(g_signal_fd, &si, sizeof si) == (ssize_t)sizeof si) {
                if (e->tarea) {
                    printf("\n[senal %s] cancelando la %s...\n",
                           si.ssi_signo == SIGINT ? "SIGINT" : "SIGTERM", nombre_op(e->tarea_op));
                    hf_job_cancel(e->tarea);
                    imprimir_prompt(e);
                } else {
                    printf("\n[senal %s] saliendo del editor.\n", si.ssi_signo == SIGINT ? "SIGINT" : "SIGTERM");
                    comando_salir(e);   /* No retorna */
                }
            }
            continue;
        }

        if (!(pfd[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;

        /* read() directo (no fgets): stdio podria dejar lineas en su buffer
         * interno y poll() no las veria. */
        ssize_t n = read(STDIN_FILENO, entrada + usados, sizeof entrada - 1 - usados);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        if (n == 0) {                               /* EOF (Ctrl+D) */
            stdin_abierto = 0;
            if (usados > 0) {
                entrada[usados] = '\0';
                usados = 0;
                modo_comando_ejecutar(e, entrada);
            }
            printf("\n");
            if (e->tarea) printf("(fin de la entrada: esperando a que termine la %s)\n", nombre_op(e->tarea_op));
            fflush(stdout);
            continue;
        }
        usados += (size_t)n;

        /* Ejecutar todas las lineas completas que llegaron. */
        size_t inicio = 0;
        for (size_t i = 0; i < usados; i++) {
            if (entrada[i] != '\n') continue;
            entrada[i] = '\0';
            if (descartando) descartando = 0;
            else             modo_comando_ejecutar(e, entrada + inicio);
            revisar_tarea(e);
            imprimir_prompt(e);
            inicio = i + 1;
        }
        memmove(entrada, entrada + inicio, usados - inicio);
        usados -= inicio;
        if (usados == sizeof entrada - 1) {         /* linea demasiado larga */
            if (!descartando)
                printf("\nLinea demasiado larga (max %zu caracteres): descartada.\n", sizeof entrada - 2);
            descartando = 1;
            usados = 0;
        }
    }
    if (g_signal_fd != -1) { close(g_signal_fd); g_signal_fd = -1; }
}
