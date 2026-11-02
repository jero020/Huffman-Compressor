/*
 * hf_concurrent.c - Motor concurrente de compresión/descompresión Huffman.
 *
 * ARQUITECTURA (ver README.md para el diagrama y la justificación):
 *
 *   Hilo UI ──hf_job_start()──► Hilo COORDINADOR ──crea──► N hilos TRABAJADORES
 *                                     │
 *                                     └── actúa como ESCRITOR ordenado
 *
 *  Compresión
 *   Fase 1 (conteo):  cada trabajador toma un índice de bloque (bajo mutex),
 *                     lo lee con pread() y cuenta en un histograma LOCAL;
 *                     al final del bloque suma su histograma al global bajo
 *                     mutex (reducción local => 256 sumas por bloque en vez
 *                     de un lock por byte).
 *   Fase 2 (código):  con el árbol global (solo lectura), los trabajadores
 *                     codifican bloques en paralelo y depositan el resultado
 *                     en una ventana circular de W ranuras. El coordinador
 *                     escribe las ranuras en orden 0,1,2,... esperando con
 *                     una variable de condición => salida ordenada, sin
 *                     espera activa y con memoria acotada (productor-consumidor).
 *
 *  Descompresión: igual que la fase 2, pero cada bloque se decodifica.
 *
 *  Primitivas usadas:
 *   - job->mtx + job->cv            : estado/progreso visible por la UI.
 *   - pipe->mtx                     : protege next_task, next_write, slots,
 *                                     freq global y abort.
 *   - pipe->cv_space                : "hay espacio en la ventana" (trabajadores esperan).
 *   - pipe->cv_ready                : "la ranura que sigue ya está lista" (escritor espera).
 *   - file_guard_t                  : lectores/escritor sobre el archivo del editor.
 */
#include "hf_concurrent.h"
#include "huffman.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define HF_MAGIC            "HUF1"
#define HF_HEADER_SIZE      (4 + 8 + 4 + 4 + HF_SYMBOLS * 8)
#define HF_CHUNK_HDR_SIZE   8
#define HF_DEFAULT_CHUNK    (1u << 20)        /* 1 MiB */
#define HF_MAX_CHUNK        (16u << 20)       /* 16 MiB */
#define HF_MAX_THREADS      64

/* ------------------------------------------------------------------ */
/* Estado del trabajo (compartido con la UI)                           */
/* ------------------------------------------------------------------ */

struct hf_job {
    pthread_t       coord;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;

    /* parámetros (inmutables tras hf_job_start) */
    hf_op_t       op;
    char         *in_path, *out_path;
    int           nthreads;
    size_t        chunk_size;
    file_guard_t *guard;

    /* protegido por mtx */
    int         cancel;
    hf_state_t  state;
    const char *phase;
    uint64_t    done, total;
    char        message[256];
};

static void job_set_phase(hf_job_t *j, const char *phase)
{
    pthread_mutex_lock(&j->mtx);
    j->phase = phase;
    pthread_cond_broadcast(&j->cv);
    pthread_mutex_unlock(&j->mtx);
}

static void job_set_total(hf_job_t *j, uint64_t total)
{
    pthread_mutex_lock(&j->mtx);
    j->total = total;
    pthread_cond_broadcast(&j->cv);
    pthread_mutex_unlock(&j->mtx);
}

static void job_progress_add(hf_job_t *j, uint64_t n)
{
    pthread_mutex_lock(&j->mtx);
    j->done += n;
    pthread_cond_broadcast(&j->cv);
    pthread_mutex_unlock(&j->mtx);
}

static int job_is_cancelled(hf_job_t *j)
{
    pthread_mutex_lock(&j->mtx);
    int c = j->cancel;
    pthread_mutex_unlock(&j->mtx);
    return c;
}

static void fill_status(hf_job_t *j, hf_status_t *st)
{
    /* Llamar con j->mtx tomado */
    st->state = j->state;
    st->phase = j->phase;
    st->done  = j->done;
    st->total = j->total;
    if (j->state == HF_STATE_DONE) st->percent = 100;
    else if (j->total == 0)        st->percent = 0;
    else                           st->percent = (int)(j->done * 100 / j->total);
    memcpy(st->message, j->message, sizeof st->message);
}

/* ------------------------------------------------------------------ */
/* E/S robusta                                                         */
/* ------------------------------------------------------------------ */

/* pread() es seguro entre hilos: no usa el offset compartido del fd. */
static int pread_full(int fd, void *buf, size_t len, uint64_t off)
{
    uint8_t *p = buf;
    while (len > 0) {
        ssize_t r = pread(fd, p, len, (off_t)off);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;                   /* EOF inesperado */
        p += r; len -= (size_t)r; off += (uint64_t)r;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; len -= (size_t)w;
    }
    return 0;
}

static void put_u32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put_u64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get_u32(const uint8_t *p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static uint64_t get_u64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }

/* ------------------------------------------------------------------ */
/* Pipeline: pool de trabajadores + ventana ordenada                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *data;
    uint32_t len;        /* bytes en data */
    uint32_t orig_len;   /* bytes originales del bloque */
    int      ready;
} slot_t;

typedef struct pipe_ctx pipe_ctx_t;
typedef int (*chunk_fn)(pipe_ctx_t *c, uint32_t idx, slot_t *out);

struct pipe_ctx {
    hf_job_t *job;
    int       in_fd, out_fd;
    uint64_t  in_size;
    uint32_t  chunk_size, nchunks;
    int       nthreads;

    hf_tree_t *tree;                 /* solo lectura durante la fase 2 */
    uint64_t   freq[HF_SYMBOLS];     /* protegido por mtx en la fase 1 */

    /* índice de bloques (solo descompresión, solo lectura en fase 2) */
    uint64_t *offsets;
    uint32_t *enc_lens, *orig_lens;

    pthread_mutex_t mtx;
    pthread_cond_t  cv_space;        /* trabajadores: hay espacio en la ventana */
    pthread_cond_t  cv_ready;        /* escritor: la siguiente ranura está lista */
    uint32_t next_task;              /* siguiente bloque por repartir */
    uint32_t next_write;             /* siguiente bloque por escribir */
    uint32_t window;                 /* número de ranuras */
    slot_t  *slots;
    chunk_fn fn;
    int      abort;
    char     err[160];
};

static void pipe_abort(pipe_ctx_t *c, const char *msg)
{
    pthread_mutex_lock(&c->mtx);
    if (!c->abort) {
        c->abort = 1;
        snprintf(c->err, sizeof c->err, "%s", msg);
    }
    /* Despertar a TODOS los que esperan para que vean abort y terminen. */
    pthread_cond_broadcast(&c->cv_space);
    pthread_cond_broadcast(&c->cv_ready);
    pthread_mutex_unlock(&c->mtx);
}

static uint32_t chunk_len(const pipe_ctx_t *c, uint32_t i)
{
    uint64_t off = (uint64_t)i * c->chunk_size;
    uint64_t rem = c->in_size - off;
    return rem < c->chunk_size ? (uint32_t)rem : c->chunk_size;
}

/* ---- Fase 1: conteo de frecuencias con reducción local ---- */

static void *freq_worker(void *arg)
{
    pipe_ctx_t *c = arg;
    uint8_t *buf = malloc(c->chunk_size);
    if (!buf) { pipe_abort(c, "sin memoria (conteo)"); return NULL; }

    for (;;) {
        pthread_mutex_lock(&c->mtx);
        if (c->abort || c->next_task >= c->nchunks) {
            pthread_mutex_unlock(&c->mtx);
            break;
        }
        uint32_t i = c->next_task++;
        pthread_mutex_unlock(&c->mtx);

        if (job_is_cancelled(c->job)) { pipe_abort(c, "cancelado"); break; }

        uint32_t len = chunk_len(c, i);
        if (pread_full(c->in_fd, buf, len, (uint64_t)i * c->chunk_size) < 0) {
            pipe_abort(c, "error leyendo el archivo de entrada");
            break;
        }

        uint64_t local[HF_SYMBOLS] = {0};       /* histograma local, sin lock */
        for (uint32_t k = 0; k < len; k++) local[buf[k]]++;

        pthread_mutex_lock(&c->mtx);            /* reducción: una vez por bloque */
        for (int s = 0; s < HF_SYMBOLS; s++) c->freq[s] += local[s];
        pthread_mutex_unlock(&c->mtx);

        job_progress_add(c->job, 1);
    }
    free(buf);
    return NULL;
}

/* ---- Fase 2: trabajadores genéricos (codificar o decodificar) ---- */

static void *pipe_worker(void *arg)
{
    pipe_ctx_t *c = arg;
    for (;;) {
        pthread_mutex_lock(&c->mtx);
        /* Ventana llena: esperar a que el escritor libere espacio (no busy-wait). */
        while (!c->abort && c->next_task < c->nchunks &&
               c->next_task >= c->next_write + c->window)
            pthread_cond_wait(&c->cv_space, &c->mtx);
        if (c->abort || c->next_task >= c->nchunks) {
            pthread_mutex_unlock(&c->mtx);
            break;
        }
        uint32_t i = c->next_task++;
        pthread_mutex_unlock(&c->mtx);

        if (job_is_cancelled(c->job)) { pipe_abort(c, "cancelado"); break; }

        slot_t res = {0};
        if (c->fn(c, i, &res) != 0) {          /* trabajo pesado SIN lock */
            free(res.data);
            pipe_abort(c, c->offsets ? "datos comprimidos corruptos o error de lectura"
                                     : "error codificando bloque");
            break;
        }

        pthread_mutex_lock(&c->mtx);
        if (c->abort) {
            pthread_mutex_unlock(&c->mtx);
            free(res.data);
            break;
        }
        /* La ranura i % window está libre: i < next_write + window garantiza
         * que el bloque i - window ya fue escrito y su ranura vaciada. */
        res.ready = 1;
        c->slots[i % c->window] = res;
        if (i == c->next_write)                 /* solo despertamos si es el que falta */
            pthread_cond_signal(&c->cv_ready);
        pthread_mutex_unlock(&c->mtx);
    }
    return NULL;
}

/* El coordinador escribe los bloques estrictamente en orden 0..nchunks-1. */
static int ordered_writer(pipe_ctx_t *c, int write_chunk_headers)
{
    for (uint32_t i = 0; i < c->nchunks; i++) {
        pthread_mutex_lock(&c->mtx);
        slot_t *s = &c->slots[i % c->window];
        while (!s->ready && !c->abort)
            pthread_cond_wait(&c->cv_ready, &c->mtx);
        if (c->abort) { pthread_mutex_unlock(&c->mtx); return -1; }
        slot_t local = *s;
        s->ready = 0;
        s->data = NULL;
        pthread_mutex_unlock(&c->mtx);

        /* La escritura a disco se hace SIN el lock: los trabajadores siguen. */
        int rc = 0;
        if (write_chunk_headers) {
            uint8_t h[HF_CHUNK_HDR_SIZE];
            put_u32(h, local.orig_len);
            put_u32(h + 4, local.len);
            rc = write_full(c->out_fd, h, sizeof h);
        }
        if (rc == 0 && local.len > 0) rc = write_full(c->out_fd, local.data, local.len);
        free(local.data);
        if (rc != 0) { pipe_abort(c, "error escribiendo el archivo de salida"); return -1; }

        pthread_mutex_lock(&c->mtx);
        c->next_write++;
        pthread_cond_broadcast(&c->cv_space);   /* liberó una ranura */
        pthread_mutex_unlock(&c->mtx);

        job_progress_add(c->job, 1);
        if (job_is_cancelled(c->job)) { pipe_abort(c, "cancelado"); return -1; }
    }
    return 0;
}

/* Lanza hasta n hilos; devuelve cuántos se crearon realmente. */
static int spawn_workers(pthread_t *tids, int n, void *(*fn)(void *), void *arg)
{
    int created = 0;
    for (int k = 0; k < n; k++) {
        if (pthread_create(&tids[k], NULL, fn, arg) != 0) break;
        created++;
    }
    return created;
}

static void join_workers(pthread_t *tids, int n)
{
    for (int k = 0; k < n; k++) pthread_join(tids[k], NULL);
}

static int run_pipeline(pipe_ctx_t *c, chunk_fn fn, int write_chunk_headers)
{
    if (c->nchunks == 0) return 0;

    int n = c->nthreads < (int)c->nchunks ? c->nthreads : (int)c->nchunks;
    c->fn = fn;
    c->next_task = 0;
    c->next_write = 0;
    c->window = (uint32_t)(2 * n);
    c->slots = calloc(c->window, sizeof(slot_t));
    if (!c->slots) { pipe_abort(c, "sin memoria (ventana)"); return -1; }

    pthread_t tids[HF_MAX_THREADS];
    int created = spawn_workers(tids, n, pipe_worker, c);
    if (created == 0) {
        pipe_abort(c, "no se pudo crear ningún hilo trabajador");
    } else {
        ordered_writer(c, write_chunk_headers);
    }
    if (c->abort) pipe_abort(c, c->err);        /* asegurar que todos despierten */
    join_workers(tids, created);

    /* Liberar resultados que quedaron en la ventana si se abortó. */
    for (uint32_t k = 0; k < c->window; k++) free(c->slots[k].data);
    free(c->slots);
    c->slots = NULL;
    return c->abort ? -1 : 0;
}

/* ---- Funciones por bloque ---- */

static int encode_chunk(pipe_ctx_t *c, uint32_t i, slot_t *out)
{
    uint32_t len = chunk_len(c, i);
    uint8_t *buf = malloc(len ? len : 1);
    if (!buf) return -1;
    if (pread_full(c->in_fd, buf, len, (uint64_t)i * c->chunk_size) < 0) { free(buf); return -1; }

    uint64_t bits = hf_encoded_bits(c->tree, buf, len);
    uint64_t bytes = (bits + 7) / 8;
    if (bytes > UINT32_MAX) { free(buf); return -1; }
    uint8_t *enc = malloc(bytes ? bytes : 1);
    if (!enc) { free(buf); return -1; }
    if (hf_encode(c->tree, buf, len, enc, bytes) != 0) { free(buf); free(enc); return -1; }
    free(buf);

    out->data = enc;
    out->len = (uint32_t)bytes;
    out->orig_len = len;
    return 0;
}

static int decode_chunk(pipe_ctx_t *c, uint32_t i, slot_t *out)
{
    uint32_t elen = c->enc_lens[i], olen = c->orig_lens[i];
    uint8_t *enc = malloc(elen ? elen : 1);
    uint8_t *dec = malloc(olen ? olen : 1);
    if (!enc || !dec) { free(enc); free(dec); return -1; }
    if (pread_full(c->in_fd, enc, elen, c->offsets[i]) < 0 ||
        hf_decode(c->tree, enc, elen, dec, olen) != 0) {
        free(enc); free(dec); return -1;
    }
    free(enc);
    out->data = dec;
    out->len = olen;
    out->orig_len = olen;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Operaciones completas (corren en el hilo coordinador)               */
/* ------------------------------------------------------------------ */

static int do_compress(pipe_ctx_t *c, const char *tmp_path)
{
    hf_job_t *j = c->job;
    struct stat stbuf;

    c->in_fd = open(j->in_path, O_RDONLY);
    if (c->in_fd < 0) { pipe_abort(c, "no se pudo abrir el archivo de entrada"); return -1; }
    if (fstat(c->in_fd, &stbuf) < 0) { pipe_abort(c, "fstat falló"); return -1; }

    c->in_size = (uint64_t)stbuf.st_size;
    c->chunk_size = (uint32_t)j->chunk_size;
    uint64_t nch = (c->in_size + c->chunk_size - 1) / c->chunk_size;
    if (nch > UINT32_MAX) { pipe_abort(c, "archivo demasiado grande"); return -1; }
    c->nchunks = (uint32_t)nch;
    job_set_total(j, 2ull * c->nchunks);

    /* Fase 1: conteo concurrente */
    job_set_phase(j, "conteo");
    if (c->nchunks > 0) {
        int n = c->nthreads < (int)c->nchunks ? c->nthreads : (int)c->nchunks;
        pthread_t tids[HF_MAX_THREADS];
        c->next_task = 0;
        int created = spawn_workers(tids, n, freq_worker, c);
        if (created == 0) pipe_abort(c, "no se pudo crear ningún hilo");
        join_workers(tids, created);              /* barrera entre fases */
        if (c->abort) return -1;
    }

    /* Árbol global: un solo hilo (coordinador), luego es de solo lectura. */
    c->tree = malloc(sizeof *c->tree);
    if (!c->tree) { pipe_abort(c, "sin memoria (árbol)"); return -1; }
    hf_tree_build(c->tree, c->freq);

    c->out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (c->out_fd < 0) { pipe_abort(c, "no se pudo crear el archivo de salida"); return -1; }

    uint8_t hdr[HF_HEADER_SIZE];
    memcpy(hdr, HF_MAGIC, 4);
    put_u64(hdr + 4, c->in_size);
    put_u32(hdr + 12, c->chunk_size);
    put_u32(hdr + 16, c->nchunks);
    for (int s = 0; s < HF_SYMBOLS; s++) put_u64(hdr + 20 + 8 * s, c->freq[s]);
    if (write_full(c->out_fd, hdr, sizeof hdr) < 0) { pipe_abort(c, "error escribiendo cabecera"); return -1; }

    /* Fase 2: codificación concurrente + escritura ordenada */
    job_set_phase(j, "codificacion");
    return run_pipeline(c, encode_chunk, 1);
}

static int do_decompress(pipe_ctx_t *c, const char *tmp_path)
{
    hf_job_t *j = c->job;
    struct stat stbuf;
    uint8_t hdr[HF_HEADER_SIZE];

    c->in_fd = open(j->in_path, O_RDONLY);
    if (c->in_fd < 0) { pipe_abort(c, "no se pudo abrir el archivo comprimido"); return -1; }
    if (fstat(c->in_fd, &stbuf) < 0) { pipe_abort(c, "fstat falló"); return -1; }
    uint64_t fsize = (uint64_t)stbuf.st_size;

    job_set_phase(j, "leyendo indice");
    if (fsize < HF_HEADER_SIZE || pread_full(c->in_fd, hdr, sizeof hdr, 0) < 0 ||
        memcmp(hdr, HF_MAGIC, 4) != 0) {
        pipe_abort(c, "no es un archivo .huf válido");
        return -1;
    }
    c->in_size    = get_u64(hdr + 4);
    c->chunk_size = get_u32(hdr + 12);
    c->nchunks    = get_u32(hdr + 16);
    uint64_t sum = 0;
    for (int s = 0; s < HF_SYMBOLS; s++) {
        c->freq[s] = get_u64(hdr + 20 + 8 * s);
        sum += c->freq[s];
    }
    if (sum != c->in_size || c->chunk_size == 0 || c->chunk_size > HF_MAX_CHUNK) {
        pipe_abort(c, "cabecera corrupta");
        return -1;
    }

    c->tree = malloc(sizeof *c->tree);
    if (!c->tree) { pipe_abort(c, "sin memoria (árbol)"); return -1; }
    hf_tree_build(c->tree, c->freq);

    /* Índice de bloques: recorrido secuencial y barato de las cabeceras
     * (8 bytes c/u) para luego decodificar los bloques en paralelo. */
    size_t n = c->nchunks ? c->nchunks : 1;
    c->offsets   = calloc(n, sizeof *c->offsets);
    c->enc_lens  = calloc(n, sizeof *c->enc_lens);
    c->orig_lens = calloc(n, sizeof *c->orig_lens);
    if (!c->offsets || !c->enc_lens || !c->orig_lens) { pipe_abort(c, "sin memoria (índice)"); return -1; }

    uint64_t pos = HF_HEADER_SIZE, total_orig = 0;
    for (uint32_t i = 0; i < c->nchunks; i++) {
        uint8_t h[HF_CHUNK_HDR_SIZE];
        if (pos + HF_CHUNK_HDR_SIZE > fsize || pread_full(c->in_fd, h, sizeof h, pos) < 0) {
            pipe_abort(c, "archivo comprimido truncado");
            return -1;
        }
        c->orig_lens[i] = get_u32(h);
        c->enc_lens[i]  = get_u32(h + 4);
        c->offsets[i]   = pos + HF_CHUNK_HDR_SIZE;
        pos = c->offsets[i] + c->enc_lens[i];
        total_orig += c->orig_lens[i];
        if (pos > fsize || c->orig_lens[i] > c->chunk_size) {
            pipe_abort(c, "archivo comprimido corrupto");
            return -1;
        }
    }
    if (total_orig != c->in_size) { pipe_abort(c, "tamaños inconsistentes"); return -1; }

    job_set_total(j, c->nchunks);
    c->out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (c->out_fd < 0) { pipe_abort(c, "no se pudo crear el archivo de salida"); return -1; }

    job_set_phase(j, "decodificacion");
    return run_pipeline(c, decode_chunk, 0);
}

static void *coordinator_main(void *arg)
{
    hf_job_t *j = arg;
    pipe_ctx_t c;
    memset(&c, 0, sizeof c);
    c.job = j;
    c.in_fd = c.out_fd = -1;
    c.nthreads = j->nthreads;

    /* Se escribe a un archivo temporal y se hace rename() atómico al final:
     * si se cancela o falla, el archivo de destino nunca queda a medias. */
    size_t tlen = strlen(j->out_path) + 8;
    char *tmp_path = malloc(tlen);

    int rc = -1;
    if (pthread_mutex_init(&c.mtx, NULL) == 0) {
        if (pthread_cond_init(&c.cv_space, NULL) == 0) {
            if (pthread_cond_init(&c.cv_ready, NULL) == 0) {
                if (!tmp_path) {
                    snprintf(c.err, sizeof c.err, "sin memoria");
                } else {
                    snprintf(tmp_path, tlen, "%s.tmp", j->out_path);
                    rc = (j->op == HF_OP_COMPRESS) ? do_compress(&c, tmp_path)
                                                   : do_decompress(&c, tmp_path);
                }
                pthread_cond_destroy(&c.cv_ready);
            }
            pthread_cond_destroy(&c.cv_space);
        }
        pthread_mutex_destroy(&c.mtx);
    }

    /* ---- Liberación de recursos en TODOS los caminos ---- */
    if (c.out_fd >= 0) {
        if (rc == 0 && fsync(c.out_fd) != 0) { rc = -1; snprintf(c.err, sizeof c.err, "fsync falló"); }
        if (close(c.out_fd) != 0 && rc == 0) { rc = -1; snprintf(c.err, sizeof c.err, "close falló"); }
    }
    if (c.in_fd >= 0) close(c.in_fd);
    if (tmp_path) {
        if (rc == 0 && rename(tmp_path, j->out_path) != 0) {
            rc = -1;
            snprintf(c.err, sizeof c.err, "rename falló: %s", strerror(errno));
        }
        if (rc != 0) unlink(tmp_path);
        free(tmp_path);
    }
    free(c.tree);
    free(c.offsets);
    free(c.enc_lens);
    free(c.orig_lens);

    /* IMPORTANTE: liberar el guard ANTES de publicar el estado final, para
     * que cuando la UI vea "terminado" ya pueda guardar el documento. */
    if (j->guard) {
        if (j->op == HF_OP_COMPRESS) file_guard_read_release(j->guard);
        else                         file_guard_write_release(j->guard);
    }

    pthread_mutex_lock(&j->mtx);
    if (rc == 0) {
        j->state = HF_STATE_DONE;
        j->phase = "terminado";
        snprintf(j->message, sizeof j->message, "%s -> %s (%llu bytes originales, %u bloques)",
                 j->in_path, j->out_path, (unsigned long long)c.in_size, c.nchunks);
    } else if (j->cancel) {
        j->state = HF_STATE_CANCELLED;
        j->phase = "cancelado";
        snprintf(j->message, sizeof j->message, "operación cancelada por el usuario");
    } else {
        j->state = HF_STATE_ERROR;
        j->phase = "error";
        snprintf(j->message, sizeof j->message, "%s", c.err[0] ? c.err : "error desconocido");
    }
    pthread_cond_broadcast(&j->cv);
    pthread_mutex_unlock(&j->mtx);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* API pública                                                         */
/* ------------------------------------------------------------------ */

hf_job_t *hf_job_start(hf_op_t op, const char *in_path, const char *out_path,
                       int nthreads, size_t chunk_size, file_guard_t *guard)
{
    if (!in_path || !out_path) { errno = EINVAL; return NULL; }

    if (nthreads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = n > 0 ? (int)n : 4;
    }
    if (nthreads > HF_MAX_THREADS) nthreads = HF_MAX_THREADS;
    if (chunk_size == 0) chunk_size = HF_DEFAULT_CHUNK;
    if (chunk_size > HF_MAX_CHUNK) chunk_size = HF_MAX_CHUNK;

    /* Tomar el guard en el hilo que llama (sin bloquear): así no hay ventana
     * entre "inicié la compresión" y "el editor guardó encima". */
    if (guard) {
        int g = (op == HF_OP_COMPRESS) ? file_guard_try_read(guard)
                                       : file_guard_try_write(guard);
        if (g != 0) { errno = EBUSY; return NULL; }
    }

    hf_job_t *j = calloc(1, sizeof *j);
    if (!j) goto fail_guard;
    j->in_path = strdup(in_path);
    j->out_path = strdup(out_path);
    if (!j->in_path || !j->out_path) goto fail_job;
    j->op = op;
    j->nthreads = nthreads;
    j->chunk_size = chunk_size;
    j->guard = guard;
    j->state = HF_STATE_RUNNING;
    j->phase = "iniciando";

    if (pthread_mutex_init(&j->mtx, NULL) != 0) goto fail_job;
    if (pthread_cond_init(&j->cv, NULL) != 0) { pthread_mutex_destroy(&j->mtx); goto fail_job; }

    /* Los hilos del motor bloquean TODAS las señales asíncronas: las señales
     * (SIGINT, SIGTERM...) las atiende la aplicación en un único lugar
     * (sigwait/signalfd) y piden cancelación con hf_job_cancel(). La máscara
     * se hereda, así que los trabajadores también quedan bloqueados. */
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    int rc = pthread_create(&j->coord, NULL, coordinator_main, j);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (rc != 0) {
        pthread_cond_destroy(&j->cv);
        pthread_mutex_destroy(&j->mtx);
        errno = rc;
        goto fail_job;
    }
    return j;

fail_job:
    if (j) { free(j->in_path); free(j->out_path); free(j); }
fail_guard:
    if (guard) {
        if (op == HF_OP_COMPRESS) file_guard_read_release(guard);
        else                      file_guard_write_release(guard);
    }
    if (errno == 0) errno = ENOMEM;
    return NULL;
}

void hf_job_get_status(hf_job_t *j, hf_status_t *st)
{
    pthread_mutex_lock(&j->mtx);
    fill_status(j, st);
    pthread_mutex_unlock(&j->mtx);
}

int hf_job_wait_update(hf_job_t *j, uint64_t last_done, int timeout_ms, hf_status_t *st)
{
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += timeout_ms / 1000;
    dl.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }

    int changed = 1;
    pthread_mutex_lock(&j->mtx);
    while (j->done == last_done && j->state == HF_STATE_RUNNING) {
        if (pthread_cond_timedwait(&j->cv, &j->mtx, &dl) == ETIMEDOUT) {
            changed = 0;
            break;
        }
    }
    if (st) fill_status(j, st);
    pthread_mutex_unlock(&j->mtx);
    return changed;
}

void hf_job_cancel(hf_job_t *j)
{
    pthread_mutex_lock(&j->mtx);
    j->cancel = 1;
    pthread_cond_broadcast(&j->cv);
    pthread_mutex_unlock(&j->mtx);
}

hf_state_t hf_job_join(hf_job_t *j, hf_status_t *st)
{
    pthread_join(j->coord, NULL);
    hf_status_t local;
    hf_job_get_status(j, &local);
    if (st) *st = local;
    pthread_cond_destroy(&j->cv);
    pthread_mutex_destroy(&j->mtx);
    free(j->in_path);
    free(j->out_path);
    free(j);
    return local.state;
}
