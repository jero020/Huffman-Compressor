/*
 * demo_editor.c - Simulación MÍNIMA de un editor para mostrar la integración.
 *
 * Reemplaza este archivo por tu editor del Parcial 1: lo importante es el
 * PATRÓN del ciclo principal, que es el mismo en ncurses, GTK o consola:
 *
 *   1. El ciclo de la UI espera eventos con poll() + timeout (no busy-wait).
 *   2. En cada vuelta consulta hf_job_get_status() y actualiza el progreso.
 *   3. El buffer del texto SOLO lo toca el hilo de UI => no necesita lock.
 *      El motor de compresión SOLO toca el archivo en disco, protegido por
 *      file_guard_t (lectores = compresión, escritor = guardar).
 *   4. Si el usuario guarda durante una compresión, el guardado queda
 *      PENDIENTE y se ejecuta cuando el trabajo termina.
 *   5. Señales: SIGINT/SIGTERM se leen con signalfd dentro del mismo poll().
 *
 * Nota: la entrada se lee con read() y un buffer propio, NO con getline()/
 * fgets(): stdio puede dejar líneas en su buffer interno y poll() no las
 * vería (el comando quedaría "dormido" hasta la siguiente tecla).
 *
 * Comandos:  a <texto> | l | w | c | x | k | s | h | q
 */
#include "file_guard.h"
#include "hf_concurrent.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

typedef struct {
    char  **lines;
    size_t  n, cap;
    int     dirty;
} buffer_t;

typedef struct {
    const char  *doc;
    char         huf[1024], restored[1024];
    buffer_t     buf;          /* solo lo usa el hilo de UI */
    file_guard_t guard;        /* protege el archivo 'doc' en disco */
    hf_job_t    *job;
    int          pending_save;
    int          last_pct;
    int          running;
} editor_t;

static int buf_append(buffer_t *b, const char *s)
{
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 16;
        char **nl = realloc(b->lines, nc * sizeof *nl);
        if (!nl) return -1;
        b->lines = nl;
        b->cap = nc;
    }
    if (!(b->lines[b->n] = strdup(s))) return -1;
    b->n++;
    b->dirty = 1;
    return 0;
}

static void buf_free(buffer_t *b)
{
    for (size_t i = 0; i < b->n; i++) free(b->lines[i]);
    free(b->lines);
}

/* Guardar = ESCRITOR del guard. Si está ocupado devuelve EBUSY sin bloquear. */
static int save_document(editor_t *e)
{
    if (file_guard_try_write(&e->guard) != 0) return EBUSY;
    FILE *f = fopen(e->doc, "w");
    int rc = 0;
    if (!f) rc = errno;
    else {
        for (size_t i = 0; i < e->buf.n; i++) fprintf(f, "%s\n", e->buf.lines[i]);
        if (fclose(f) != 0) rc = errno;
    }
    file_guard_write_release(&e->guard);
    if (rc == 0) e->buf.dirty = 0;
    return rc;
}

static void help(void)
{
    puts("Comandos:\n"
         "  a <texto>  agregar línea (se puede editar MIENTRAS se comprime)\n"
         "  l          listar buffer\n"
         "  w          guardar en disco (queda pendiente si hay compresión)\n"
         "  c          comprimir el documento en segundo plano -> <doc>.huf\n"
         "  x          descomprimir <doc>.huf -> <doc>.restaurado\n"
         "  k          cancelar trabajo en curso\n"
         "  s          estado del trabajo\n"
         "  q          salir");
}

/* Llamada en cada vuelta del ciclo: refresca progreso y recoge el trabajo. */
static void poll_job(editor_t *e)
{
    if (!e->job) return;
    hf_status_t st;
    hf_job_get_status(e->job, &st);
    if (st.state == HF_STATE_RUNNING) {
        if (st.percent / 10 != e->last_pct / 10) {    /* aquí iría tu barra de progreso */
            printf("  [progreso] %3d%% (%s)\n", st.percent, st.phase);
            fflush(stdout);
            e->last_pct = st.percent;
        }
        return;
    }
    hf_job_join(e->job, &st);                        /* join + liberar recursos */
    e->job = NULL;
    printf("  [trabajo] %s: %s\n",
           st.state == HF_STATE_DONE ? "OK" :
           st.state == HF_STATE_CANCELLED ? "CANCELADO" : "ERROR", st.message);
    if (e->pending_save) {                           /* guardado diferido */
        int rc = save_document(e);
        printf("  [guardado pendiente] %s\n", rc == 0 ? "guardado" : strerror(rc));
        e->pending_save = 0;
    }
    fflush(stdout);
}

static void handle_command(editor_t *e, char *line)
{
    switch (line[0]) {
    case 'a':
        buf_append(&e->buf, line[1] == ' ' ? line + 2 : line + 1);
        break;
    case 'l':
        for (size_t i = 0; i < e->buf.n; i++) printf("%4zu| %s\n", i + 1, e->buf.lines[i]);
        break;
    case 'w': {
        int rc = save_document(e);
        if (rc == EBUSY) {
            e->pending_save = 1;
            puts("  El archivo está siendo comprimido: el guardado queda PENDIENTE.");
        } else printf("  %s\n", rc == 0 ? "guardado" : strerror(rc));
        break;
    }
    case 'c':
    case 'x':
        if (e->job) { puts("  Ya hay un trabajo en curso."); break; }
        if (line[0] == 'c' && e->buf.dirty) {        /* comprimir la última versión */
            int rc = save_document(e);
            if (rc != 0) { printf("  no se pudo guardar: %s\n", strerror(rc)); break; }
        }
        e->job = (line[0] == 'c')
            ? hf_job_start(HF_OP_COMPRESS, e->doc, e->huf, 0, 0, &e->guard)
            : hf_job_start(HF_OP_DECOMPRESS, e->huf, e->restored, 0, 0, NULL);
        if (!e->job) printf("  no se pudo iniciar: %s\n", strerror(errno));
        else { e->last_pct = -1; puts("  trabajo iniciado en segundo plano (puedes seguir editando)"); }
        break;
    case 'k':
        if (e->job) hf_job_cancel(e->job); else puts("  no hay trabajo en curso");
        break;
    case 's':
        if (e->job) {
            hf_status_t st;
            hf_job_get_status(e->job, &st);
            printf("  %d%% fase=%s bloques=%llu/%llu\n", st.percent, st.phase,
                   (unsigned long long)st.done, (unsigned long long)st.total);
        } else puts("  sin trabajo en curso");
        break;
    case 'h': help(); break;
    case 'q': e->running = 0; break;
    case '\0': break;
    default: puts("  comando desconocido (h = ayuda)");
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    editor_t e;
    memset(&e, 0, sizeof e);
    e.doc = argc > 1 ? argv[1] : "documento.txt";
    snprintf(e.huf, sizeof e.huf, "%s.huf", e.doc);
    snprintf(e.restored, sizeof e.restored, "%s.restaurado", e.doc);
    e.running = 1;
    e.last_pct = -1;

    /* Señales por signalfd (se bloquean antes de crear hilos). */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) { perror("signalfd"); return 1; }

    if (file_guard_init(&e.guard) != 0) { fprintf(stderr, "guard init falló\n"); return 1; }

    FILE *f = fopen(e.doc, "r");                      /* cargar documento existente */
    if (f) {
        char *line = NULL; size_t cap = 0; ssize_t r;
        while ((r = getline(&line, &cap, f)) > 0) {
            if (line[r - 1] == '\n') line[r - 1] = '\0';
            buf_append(&e.buf, line);
        }
        free(line);
        fclose(f);
        e.buf.dirty = 0;
    }
    printf("Demo editor sobre '%s' (%zu líneas). Escribe 'h' para ayuda.\n", e.doc, e.buf.n);
    fflush(stdout);

    char   in[4096];
    size_t inlen = 0;
    int    stdin_open = 1;

    while (e.running) {
        poll_job(&e);

        /* Esperar entrada o señal SIN espera activa. Con trabajo activo se
         * despierta cada 100 ms para refrescar el progreso. */
        struct pollfd pfd[2] = { { stdin_open ? STDIN_FILENO : -1, POLLIN, 0 },
                                 { sfd, POLLIN, 0 } };
        int timeout = e.job ? 100 : -1;
        if (!stdin_open && !e.job) break;            /* EOF y nada pendiente */
        int pr = poll(pfd, 2, timeout);
        if (pr < 0) { if (errno == EINTR) continue; perror("poll"); break; }
        if (pr == 0) continue;

        if (pfd[1].revents & POLLIN) {
            struct signalfd_siginfo si;
            if (read(sfd, &si, sizeof si) == sizeof si) {
                if (e.job) { puts("\n[señal] cancelando trabajo..."); hf_job_cancel(e.job); }
                else e.running = 0;
            }
            continue;
        }
        if (!(pfd[0].revents & (POLLIN | POLLHUP))) continue;

        ssize_t r = read(STDIN_FILENO, in + inlen, sizeof in - 1 - inlen);
        if (r < 0) { if (errno == EINTR) continue; perror("read"); break; }
        if (r == 0) {                                 /* EOF: termina al acabar el trabajo */
            stdin_open = 0;
            if (inlen > 0) { in[inlen] = '\0'; handle_command(&e, in); inlen = 0; }
            continue;
        }
        inlen += (size_t)r;

        /* Procesar todas las líneas completas que llegaron. */
        size_t start = 0;
        for (size_t i = 0; i < inlen && e.running; i++) {
            if (in[i] != '\n') continue;
            in[i] = '\0';
            handle_command(&e, in + start);
            poll_job(&e);
            start = i + 1;
        }
        memmove(in, in + start, inlen - start);
        inlen -= start;
        if (inlen == sizeof in - 1) {                 /* línea demasiado larga */
            in[inlen] = '\0';
            handle_command(&e, in);
            inlen = 0;
        }
    }

    /* Salida limpia: cancelar y unir el trabajo, liberar todo. */
    if (e.job) { hf_job_cancel(e.job); hf_job_join(e.job, NULL); }
    if (e.buf.dirty) puts("Aviso: había cambios sin guardar.");
    buf_free(&e.buf);
    file_guard_destroy(&e.guard);
    close(sfd);
    return 0;
}
