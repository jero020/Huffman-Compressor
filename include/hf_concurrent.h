/*
 * hf_concurrent.h - API asíncrona de compresión/descompresión Huffman.
 *
 * Esta es la ÚNICA interfaz que el editor (Parcial 1) necesita conocer:
 *
 *     hf_job_t *job = hf_job_start(HF_OP_COMPRESS, "doc.txt", "doc.huf",
 *                                  0, 0, &guard_del_documento);
 *     ...en el ciclo de la UI...
 *     hf_job_get_status(job, &st);          // nunca bloquea
 *     dibujar_barra(st.percent, st.phase);
 *     if (st.state != HF_STATE_RUNNING) hf_job_join(job, &st);
 *
 * hf_job_start() retorna de inmediato: todo el trabajo lo hace un hilo
 * coordinador en segundo plano que, a su vez, lanza un pool de hilos
 * trabajadores. La UI nunca se bloquea.
 */
#ifndef HF_CONCURRENT_H
#define HF_CONCURRENT_H

#include <stddef.h>
#include <stdint.h>

#include "file_guard.h"

typedef enum { HF_OP_COMPRESS, HF_OP_DECOMPRESS } hf_op_t;

typedef enum {
    HF_STATE_RUNNING,
    HF_STATE_DONE,
    HF_STATE_ERROR,
    HF_STATE_CANCELLED
} hf_state_t;

typedef struct {
    hf_state_t  state;
    int         percent;      /* 0..100 */
    const char *phase;        /* "iniciando", "conteo", "codificacion", ... */
    uint64_t    done, total;  /* unidades de trabajo (bloques procesados) */
    char        message[256]; /* mensaje de error o resumen final */
    double      t_conteo, t_arbol, t_codificacion; /* segundos por fase (0 si no aplica o aún no termina) */
} hf_status_t;

typedef struct hf_job hf_job_t;

/*
 * Inicia un trabajo en segundo plano.
 *   nthreads   : 0 = número de núcleos.
 *   chunk_size : 0 = 1 MiB por bloque.
 *   guard      : puede ser NULL. Si no es NULL:
 *                 - COMPRESS   toma el guard como LECTOR  (protege 'in').
 *                 - DECOMPRESS toma el guard como ESCRITOR (protege 'out').
 *                El guard se toma aquí (en el hilo que llama) y se libera
 *                en el coordinador ANTES de publicar el estado final.
 * Retorna NULL y errno = EBUSY si el guard está ocupado, u otro errno.
 */
hf_job_t *hf_job_start(hf_op_t op, const char *in_path, const char *out_path,
                       int nthreads, size_t chunk_size, file_guard_t *guard);

/* Copia el estado actual. No bloquea (solo toma un mutex brevemente). */
void hf_job_get_status(hf_job_t *job, hf_status_t *st);

/* Espera (sin espera activa, con pthread_cond_timedwait) hasta que el
 * progreso avance más allá de last_done, el trabajo termine, o pase
 * timeout_ms. Retorna 1 si hubo cambio, 0 si fue timeout. */
int hf_job_wait_update(hf_job_t *job, uint64_t last_done, int timeout_ms,
                       hf_status_t *st);

/* Solicita cancelación (asíncrona). Seguro de llamar desde cualquier hilo. */
void hf_job_cancel(hf_job_t *job);

/* Espera al coordinador (pthread_join), copia el estado final y libera
 * todos los recursos del trabajo. 'st' puede ser NULL. */
hf_state_t hf_job_join(hf_job_t *job, hf_status_t *st);

#endif
