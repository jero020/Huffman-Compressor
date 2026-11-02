#include "file_guard.h"

#include <errno.h>

int file_guard_init(file_guard_t *g)
{
    g->readers = 0;
    g->writer = 0;
    if (pthread_mutex_init(&g->mtx, NULL) != 0) return -1;
    if (pthread_cond_init(&g->cv, NULL) != 0) {
        pthread_mutex_destroy(&g->mtx);
        return -1;
    }
    return 0;
}

void file_guard_destroy(file_guard_t *g)
{
    pthread_cond_destroy(&g->cv);
    pthread_mutex_destroy(&g->mtx);
}

int file_guard_try_read(file_guard_t *g)
{
    int rc = 0;
    pthread_mutex_lock(&g->mtx);
    if (g->writer) rc = EBUSY;
    else g->readers++;
    pthread_mutex_unlock(&g->mtx);
    return rc;
}

void file_guard_read_release(file_guard_t *g)
{
    pthread_mutex_lock(&g->mtx);
    if (--g->readers == 0) pthread_cond_broadcast(&g->cv);
    pthread_mutex_unlock(&g->mtx);
}

int file_guard_try_write(file_guard_t *g)
{
    int rc = 0;
    pthread_mutex_lock(&g->mtx);
    if (g->writer || g->readers > 0) rc = EBUSY;
    else g->writer = 1;
    pthread_mutex_unlock(&g->mtx);
    return rc;
}

void file_guard_write_acquire(file_guard_t *g)
{
    pthread_mutex_lock(&g->mtx);
    while (g->writer || g->readers > 0)          /* while, no if: despertares espurios */
        pthread_cond_wait(&g->cv, &g->mtx);
    g->writer = 1;
    pthread_mutex_unlock(&g->mtx);
}

void file_guard_write_release(file_guard_t *g)
{
    pthread_mutex_lock(&g->mtx);
    g->writer = 0;
    pthread_cond_broadcast(&g->cv);
    pthread_mutex_unlock(&g->mtx);
}

int file_guard_is_busy(file_guard_t *g)
{
    pthread_mutex_lock(&g->mtx);
    int busy = g->writer || g->readers > 0;
    pthread_mutex_unlock(&g->mtx);
    return busy;
}
