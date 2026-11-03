/*
 * huffcli.c - Interfaz de línea de comandos del compresor concurrente.
 *
 *   ./huffcli -c entrada salida.huf [-t hilos] [-s KB_por_bloque]
 *   ./huffcli -d entrada.huf salida [-t hilos]
 *
 * Manejo de señales: SIGINT/SIGTERM se BLOQUEAN en todos los hilos y un
 * hilo dedicado las recibe con sigwait(). Así la señal nunca interrumpe a
 * un hilo con un mutex tomado y la cancelación es limpia (join + free).
 */
#include "hf_concurrent.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t g_job_mtx = PTHREAD_MUTEX_INITIALIZER;
static hf_job_t *g_job = NULL;

static void *signal_thread(void *arg)
{
    sigset_t *set = arg;
    for (;;) {
        int sig;
        if (sigwait(set, &sig) != 0) continue;
        if (sig == SIGUSR1) break;                 /* el main pide terminar */
        pthread_mutex_lock(&g_job_mtx);
        if (g_job) {
            fprintf(stderr, "\n[señal %d] cancelando...\n", sig);
            hf_job_cancel(g_job);
        }
        pthread_mutex_unlock(&g_job_mtx);
    }
    return NULL;
}

static void draw_bar(const hf_status_t *st)
{
    int width = 40, filled = st->percent * width / 100;
    fprintf(stderr, "\r[");
    for (int i = 0; i < width; i++) fputc(i < filled ? '#' : '.', stderr);
    fprintf(stderr, "] %3d%%  %-16s", st->percent, st->phase);
    fflush(stderr);
}

static void usage(const char *p)
{
    fprintf(stderr,
            "Uso: %s -c|-d ENTRADA SALIDA [-t hilos] [-s KB_por_bloque]\n"
            "  -c  comprimir    -d  descomprimir\n", p);
}

int main(int argc, char **argv)
{
    if (argc < 4) { usage(argv[0]); return 2; }

    hf_op_t op;
    if (strcmp(argv[1], "-c") == 0) op = HF_OP_COMPRESS;
    else if (strcmp(argv[1], "-d") == 0) op = HF_OP_DECOMPRESS;
    else { usage(argv[0]); return 2; }

    const char *in = argv[2], *out = argv[3];
    int threads = 0;
    size_t chunk = 0;
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) threads = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) chunk = (size_t)atol(argv[++i]) * 1024;
        else { usage(argv[0]); return 2; }
    }

    /* Bloquear señales ANTES de crear cualquier hilo (la máscara se hereda). */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    pthread_t sig_tid;
    if (pthread_create(&sig_tid, NULL, signal_thread, &set) != 0) {
        perror("pthread_create");
        return 1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pthread_mutex_lock(&g_job_mtx);
    g_job = hf_job_start(op, in, out, threads, chunk, NULL);
    hf_job_t *job = g_job;
    pthread_mutex_unlock(&g_job_mtx);
    if (!job) {
        fprintf(stderr, "No se pudo iniciar: %s\n", strerror(errno));
        pthread_kill(sig_tid, SIGUSR1);
        pthread_join(sig_tid, NULL);
        return 1;
    }

    /* "UI": espera eventos de progreso SIN espera activa. */
    hf_status_t st;
    hf_job_get_status(job, &st);
    int last_pct = -1;
    const char *last_phase = NULL;
    while (st.state == HF_STATE_RUNNING) {
        if (st.percent != last_pct || st.phase != last_phase) {  /* redibujar solo si cambia */
            draw_bar(&st);
            last_pct = st.percent;
            last_phase = st.phase;
        }
        hf_job_wait_update(job, st.done, 250, &st);
    }
    draw_bar(&st);
    fputc('\n', stderr);

    pthread_mutex_lock(&g_job_mtx);
    g_job = NULL;                                  /* el hilo de señales ya no lo usa */
    pthread_mutex_unlock(&g_job_mtx);
    hf_state_t final = hf_job_join(job, &st);

    pthread_kill(sig_tid, SIGUSR1);                /* terminar hilo de señales */
    pthread_join(sig_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    fprintf(stderr, "%s (%.3f s)\n", st.message, secs);
    return final == HF_STATE_DONE ? 0 : (final == HF_STATE_CANCELLED ? 130 : 1);
}
