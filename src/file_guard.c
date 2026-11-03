#include "file_guard.h"

#include <errno.h>

int file_guard_init(file_guard_t *g)
{
    g->readers = 0;
    g->writer = 0;
    return pthread_mutex_init(&g->mtx, NULL) != 0 ? -1 : 0;
}

void file_guard_destroy(file_guard_t *g)
{
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
    g->readers--;
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

void file_guard_write_release(file_guard_t *g)
{
    pthread_mutex_lock(&g->mtx);
    g->writer = 0;
    pthread_mutex_unlock(&g->mtx);
}
