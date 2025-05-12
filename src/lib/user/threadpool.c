// work helping, single deque/lock
#include "threadpool.h"
#include "lib/kernel/list.h"
#include "lib/user/mm.h"
#include <stdio.h>


// sent when creating a thread
static void *worker_body(void *arg);

// T if no tasks available, F if there are
// static bool cannot_process_tasks(void);

enum future_status
{
    SUBMITTED,
    EXECUTING,
    COMPLETED
};

struct thread_pool
{
    // global queue
    struct list global_queue;
    pthread_mutex_t thread_pool_lock;
    pthread_cond_t thread_pool_cond;
    bool shutdown_flag; // T if shutdown, F if not
    struct worker *workers;
    int nthreads;
    int * worker_tids;
};

struct worker
{
    struct thread_pool *pool;
    // struct list internal_deque; // front is bottom, back is top
};

struct future
{
    fork_join_task_t task;
    void *args;
    void *result;
    // invoke task by fut->result = fut->task(pool, fut->data)
    enum future_status status;
    struct list_elem elem;
    struct thread_pool *pool;
    struct worker *worker;
};

static void *worker_body(void *arg)
{
    set_tls(0, arg);
    struct worker * current_worker = (struct worker *)get_tls(0);
    struct thread_pool *pool = current_worker->pool;

    pthread_mutex_lock(&current_worker->pool->thread_pool_lock);

    while (!pool->shutdown_flag)
    {
        if (!list_empty(&pool->global_queue))
        {
            struct list_elem *fut_elem = list_pop_back(&pool->global_queue);
            struct future *fut = list_entry(fut_elem, struct future, elem);

            fut->status = EXECUTING;

            pthread_mutex_unlock(&current_worker->pool->thread_pool_lock);

            fut->result = fut->task(pool, fut->args);

            pthread_mutex_lock(&current_worker->pool->thread_pool_lock);

            pthread_cond_broadcast(&pool->thread_pool_cond, &current_worker->pool->thread_pool_lock);

            fut->status = COMPLETED;

            pthread_mutex_unlock(&current_worker->pool->thread_pool_lock);

            pthread_mutex_lock(&current_worker->pool->thread_pool_lock);

            continue;
        }

        pthread_cond_wait(&pool->thread_pool_cond, &pool->thread_pool_lock);
    }

    pthread_mutex_unlock(&current_worker->pool->thread_pool_lock);

    return NULL;
}

/* Create a new thread pool with no more than n threads.
 * If any of the threads cannot be created, print
 * an error message and return NULL. */
struct thread_pool *thread_pool_new(int nthreads)
{
    // create thread pool
    struct thread_pool *pool = _mm_malloc(sizeof(struct thread_pool), mem_lock);

    list_init(&pool->global_queue);
    pthread_mutex_init(&pool->thread_pool_lock);
    pthread_cond_init(&pool->thread_pool_cond);
    pool->shutdown_flag = false;
    pool->nthreads = nthreads;
    pool->worker_tids = _mm_malloc(sizeof(int) * nthreads, mem_lock);

    pthread_mutex_lock(&pool->thread_pool_lock); // worker can't go into the loop until submit

    // initialize worker threads
    pool->workers = _mm_malloc(nthreads * sizeof(struct worker), mem_lock);

    for (int i = 0; i < nthreads; i++)
    {
        struct worker *curr_worker = &pool->workers[i];
        curr_worker->pool = pool;
        // list_init(&curr_worker->internal_deque);

        pool->worker_tids[i] = _pthread_create(worker_body, curr_worker);
        if (pool->worker_tids[i] == -1)
        {
            printf("thread creation error");
            pthread_mutex_unlock(&pool->thread_pool_lock);
            return NULL;
        }
    }

    pthread_mutex_unlock(&pool->thread_pool_lock);

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
    pthread_cond_broadcast(&pool->thread_pool_cond, &pool->thread_pool_lock);

    pthread_mutex_unlock(&pool->thread_pool_lock);

    // pthread_mutex_lock(&pool->thread_pool_lock);

    for (int i = 0; i < pool->nthreads; i++)
    {
        // struct worker *curr_worker = &pool->workers[i];
        pthread_join(pool->worker_tids[i], NULL);
    }

    _mm_free(pool->workers, mem_lock);
    _mm_free(pool->worker_tids, mem_lock);

    pthread_mutex_destroy(&pool->thread_pool_lock);
    _mm_free(pool, mem_lock);

    pthread_cond_destroy(&pool->thread_pool_cond);
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
    struct worker * current_worker = (struct worker *)get_tls(0);

    pthread_mutex_lock(&pool->thread_pool_lock);

    struct future *fut;
    fut = _mm_malloc(sizeof(struct future), mem_lock);
    fut->task = task;
    fut->args = data;
    fut->result = NULL;
    fut->status = SUBMITTED;
    fut->pool = pool;

    fut->worker = current_worker;

    list_push_back(&pool->global_queue, &fut->elem);

    pthread_cond_signal(&pool->thread_pool_cond, &pool->thread_pool_lock); // signal or broadcast

    pthread_mutex_unlock(&pool->thread_pool_lock);

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

    pthread_mutex_lock(&pool->thread_pool_lock);

    if (fut->worker == NULL)
    {
        while (fut->status != COMPLETED)
        {
            pthread_cond_wait(&pool->thread_pool_cond, &pool->thread_pool_lock);
        }
    }
    else
    {
        if (fut->status == SUBMITTED)
        {
            list_remove(&fut->elem);
            fut->status = EXECUTING;

            pthread_mutex_unlock(&pool->thread_pool_lock);

            fut->result = fut->task(pool, fut->args);

            pthread_mutex_lock(&pool->thread_pool_lock);

            fut->status = COMPLETED;

            pthread_cond_signal(&pool->thread_pool_cond, &pool->thread_pool_lock);
        }
        else if (fut->status == EXECUTING)
        {
            while (fut->status != COMPLETED)
            {
                pthread_cond_wait(&pool->thread_pool_cond, &pool->thread_pool_lock);
            }
        }
    }

    pthread_mutex_unlock(&pool->thread_pool_lock);

    return fut->result;
}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *fut)
{
    // frees the memory for a future instance allocated in thread_pool_submit
    _mm_free(fut, mem_lock);
}