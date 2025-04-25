#define _GNU_SOURCE
#include "threadpool.h"
#include "lib/kernel/list.h"
#include "lib/user/mm.h"

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

// start routine of a thread
static void *worker_body(void *arg);
static pthread_mutex_t mem_lock;

// future status states
enum future_status
{
    SUBMITTED,
    EXECUTING,
    COMPLETED
};

// thread_pool struct
struct thread_pool
{
    pthread_mutex_t thread_pool_lock;
    bool shutdown_flag; // T if shutdown, F if not
    struct worker *workers;
    int nthreads;

    struct list global_queue;
    char padding[32];
};

// worker struct
struct worker
{
    int thread; // maybe swap wit lock
    struct thread_pool *pool;
    pthread_mutex_t worker_lock;

    char padding[8];
    struct list internal_deque;
    char padding2[32];
};

// future struct
struct future
{
    struct worker *worker;
    struct worker *steal_worker;
    struct list_elem elem;
    char padding[32];

    fork_join_task_t task;
    void *args;
    void *result;
    char padding1[40];

    enum future_status status;
    struct thread_pool *pool;
    pthread_cond_t future_cond; // tells when future is complete
};

// start routine of a thread
static void *worker_body(void *arg)
{
    set_tls(data, arg);
    struct worker * current_worker = (struct worker *)get_tls(data);

    struct thread_pool *pool = current_worker->pool;

    pthread_mutex_lock(&pool->thread_pool_lock);

    while (!pool->shutdown_flag)
    {
        if (!list_empty(&pool->global_queue))
        {
            struct list_elem *fut_elem = list_pop_back(&pool->global_queue);

            struct future *fut = list_entry(fut_elem, struct future, elem);

            fut->status = EXECUTING;

            pthread_mutex_unlock(&pool->thread_pool_lock);

            fut->result = fut->task(pool, fut->args);

            pthread_mutex_lock(&pool->thread_pool_lock);

            fut->status = COMPLETED;

            pthread_cond_signal(&fut->future_cond);

            pthread_mutex_unlock(&pool->thread_pool_lock);

            pthread_mutex_lock(&pool->thread_pool_lock);

            continue;
        }
        else
        {
            pthread_mutex_unlock(&pool->thread_pool_lock);

            for (int i = 0; i < pool->nthreads; i++)
            {
                struct worker *steal = &pool->workers[i];

                if (steal == current_worker)
                {
                    continue;
                }

                pthread_mutex_lock(&steal->worker_lock);

                if (!list_empty(&steal->internal_deque))
                {
                    struct list_elem *fut_elem = list_pop_back(&steal->internal_deque);

                    struct future *fut = list_entry(fut_elem, struct future, elem);

                    fut->steal_worker = current_worker;

                    fut->status = EXECUTING;

                    pthread_mutex_unlock(&steal->worker_lock);

                    fut->result = fut->task(fut->pool, fut->args);

                    pthread_mutex_lock(&steal->worker_lock);

                    fut->status = COMPLETED;

                    pthread_cond_signal(&fut->future_cond);

                    pthread_mutex_unlock(&steal->worker_lock);

                    continue; // steal once per worker
                }

                pthread_mutex_unlock(&steal->worker_lock);
            }
        }

        // usleep(1000);

        pthread_mutex_lock(&pool->thread_pool_lock);
    }

    pthread_mutex_unlock(&pool->thread_pool_lock);

    return NULL;
}

/* Create a new thread pool with no more than n threads.
 * If any of the threads cannot be created, print
 * an error message and return NULL. */
struct thread_pool *thread_pool_new(int nthreads)
{
    pthread_mutex_init(&mem_lock);

    struct thread_pool *pool = _mm_malloc(sizeof(struct thread_pool), mem_lock);

    list_init(&pool->global_queue);
    pthread_mutex_init(&pool->thread_pool_lock, NULL);
    pool->shutdown_flag = false;
    pool->nthreads = nthreads;

    pool->workers = _mm_malloc(nthreads * sizeof(struct worker), mem_lock);

    for (int i = 0; i < nthreads; i++)
    {
        struct worker *curr_worker = &pool->workers[i];
        curr_worker->pool = pool;
        list_init(&curr_worker->internal_deque);
        pthread_mutex_init(&curr_worker->worker_lock, NULL);
    }

    for (int i = 0; i < nthreads; i++)
    {
        struct worker *curr_worker = &pool->workers[i];
        if (_pthread_create(worker_body, curr_worker) != 0)
        {
            printf("thread creation error");
            return NULL;
        }
    }
    return pool;
}

/*
 * Shutdown this thread pool in an orderly fashion.
 * Tasks that have been submitted but not executed may or
 * may not be executed.
 *
 * Deallocate the thread pool object before returning.
 */
void thread_pool_shutdown_and_destroy(struct thread_pool *pool)
{
    pthread_mutex_lock(&pool->thread_pool_lock);

    pool->shutdown_flag = true;

    pthread_mutex_unlock(&pool->thread_pool_lock);

    for (int i = 0; i < pool->nthreads; i++)
    {
        struct worker *curr_worker = &pool->workers[i];
        pthread_join(curr_worker->thread, NULL);
        pthread_mutex_destroy(&curr_worker->worker_lock);
    }

    _mm_free(pool->workers, mem_lock);

    pthread_mutex_destroy(&pool->thread_pool_lock);
    _mm_free(pool, mem_lock);
}

/*
 * Submit a fork join task to the thread pool and return a
 * future.  The returned future can be used in future_get()
 * to obtain the result.
 * ’pool’ - the pool to which to submit
 * ’task’ - the task to be submitted.
 * ’data’ - data to be passed to the task’s function
 *
 * Returns a future representing this computation.
 */
struct future *thread_pool_submit(
    struct thread_pool *pool,
    fork_join_task_t task,
    void *data)
{
    struct worker * current_worker = (struct worker *)get_tls(data);

    struct future *fut;
    fut = _mm_malloc(sizeof(struct future), mem_lock);
    fut->task = task;
    fut->args = data;
    fut->result = NULL;
    fut->status = SUBMITTED;
    fut->pool = pool;
    fut->worker = NULL;
    pthread_cond_init(&fut->future_cond, NULL);

    if (current_worker == NULL)
    {
        pthread_mutex_lock(&pool->thread_pool_lock);

        list_push_front(&pool->global_queue, &fut->elem);

        pthread_mutex_unlock(&pool->thread_pool_lock);
    }
    else
    {
        pthread_mutex_lock(&current_worker->worker_lock);

        fut->worker = current_worker;

        list_push_front(&current_worker->internal_deque, &fut->elem);

        pthread_mutex_unlock(&current_worker->worker_lock);
    }

    return fut;
}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void *future_get(struct future *fut)
{
    struct thread_pool *pool = fut->pool;
    struct worker * current_worker = (struct worker *)get_tls(data);

    if (current_worker == NULL)
    {
        pthread_mutex_lock(&pool->thread_pool_lock);

        while (fut->status != COMPLETED)
        {
            pthread_cond_wait(&fut->future_cond, &pool->thread_pool_lock);
        }

        pthread_mutex_unlock(&pool->thread_pool_lock);

        return fut->result;
    }

    pthread_mutex_lock(&fut->worker->worker_lock);

    if (fut->status == SUBMITTED)
    {
        list_remove(&fut->elem);

        fut->status = EXECUTING;

        pthread_mutex_unlock(&fut->worker->worker_lock);

        fut->result = fut->task(pool, fut->args);

        pthread_mutex_lock(&fut->worker->worker_lock);

        fut->status = COMPLETED;

        pthread_mutex_unlock(&fut->worker->worker_lock);

        return fut->result;
    }

    while (fut->status != COMPLETED)
    {
        pthread_mutex_unlock(&fut->worker->worker_lock);

        pthread_mutex_lock(&fut->steal_worker->worker_lock);

        if (!list_empty(&fut->steal_worker->internal_deque))
        {
            struct list_elem *fut_elem = list_pop_back(&fut->steal_worker->internal_deque);

            struct future *fut_help = list_entry(fut_elem, struct future, elem);

            fut_help->steal_worker = current_worker;

            fut_help->status = EXECUTING;

            pthread_mutex_unlock(&fut->steal_worker->worker_lock);

            fut_help->result = fut_help->task(pool, fut_help->args);

            pthread_mutex_lock(&fut->steal_worker->worker_lock);

            fut_help->status = COMPLETED;

            pthread_cond_signal(&fut_help->future_cond);

            pthread_mutex_unlock(&fut->steal_worker->worker_lock);
        }
        else
        {
            pthread_mutex_unlock(&fut->steal_worker->worker_lock);

            pthread_mutex_lock(&fut->worker->worker_lock);

            break; // and sleep
        }

        pthread_mutex_lock(&fut->worker->worker_lock);
    }

    while (fut->status != COMPLETED)
    {
        pthread_cond_wait(&fut->future_cond, &fut->worker->worker_lock);
    }

    pthread_mutex_unlock(&fut->worker->worker_lock);

    return fut->result;
}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *fut)
{
    // _mm_frees the memory for a future instance allocated in thread_pool_submit
    pthread_cond_destroy(&fut->future_cond);
    _mm_free(fut, mem_lock);
}