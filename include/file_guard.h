/*
 * file_guard.h - Protección del archivo del editor frente a accesos concurrentes.
 *
 * Es un cerrojo lectores/escritor construido con mutex + variable de condición
 * (en lugar de pthread_rwlock) por una razón concreta: el cerrojo se ADQUIERE
 * en el hilo de la interfaz (al iniciar la compresión) y se LIBERA en el hilo
 * coordinador del trabajo cuando termina. POSIX dice que desbloquear un
 * pthread_rwlock desde un hilo distinto al que lo tomó es comportamiento
 * indefinido; con contadores protegidos por mutex esto sí es válido.
 *
 *  - Lectores: trabajos de compresión que leen el archivo del documento.
 *  - Escritor: el editor cuando guarda, o una descompresión que sobrescribe
 *    el documento.
 *
 * Las funciones try_* nunca bloquean: la UI las usa para no congelarse.
 */
#ifndef FILE_GUARD_H
#define FILE_GUARD_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    int readers;   /* número de lectores activos */
    int writer;    /* 1 si hay un escritor activo */
} file_guard_t;

int  file_guard_init(file_guard_t *g);
void file_guard_destroy(file_guard_t *g);

int  file_guard_try_read(file_guard_t *g);    /* 0 o EBUSY */
void file_guard_read_release(file_guard_t *g);

int  file_guard_try_write(file_guard_t *g);   /* 0 o EBUSY */
void file_guard_write_acquire(file_guard_t *g); /* bloqueante (sin espera activa) */
void file_guard_write_release(file_guard_t *g);

int  file_guard_is_busy(file_guard_t *g);

#endif
