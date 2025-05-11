#include <syscall.h>

/**
 * threadpool.h
 *
 * A work-stealing, fork-join thread pool.
 */
/*
 * Opaque forward declarations. The actual definitions of these
 * types will be local to your threadpool.c implementation.
 */

struct thread_pool;
struct future;
extern pthread_mutex_t mem_lock;

struct thread_pool *thread_pool_new(int nthreads);

void thread_pool_shutdown_and_destroy(struct thread_pool *);

typedef void *(*fork_join_task_t)(struct thread_pool *pool, void *data);

struct future *thread_pool_submit(
    struct thread_pool *pool,
    fork_join_task_t task,
    void *data);

void *future_get(struct future *);

void future_free(struct future *);