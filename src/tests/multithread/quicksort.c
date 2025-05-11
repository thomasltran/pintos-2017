#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include "lib/user/threadpool.h"
#include "lib/user/mm.h"
#include <stdio.h>
#include "lib/random.h"
#include <string.h>

#include <stdlib.h>

#define NUM_THREADS 32

char mymemory[128 * 1024 * 1024]; // set chunk of memory
pthread_mutex_t mem_lock;

typedef void (*sort_func)(int *, int);
// ./quicksort -n 16 -s 44 -d 15 30000000
static int nthreads = NUM_THREADS;
static int seed = 44;
static int depth = 18;
static int N = 3000000; // N * sizeof(int) * 2 bc 2 malloc calls with this

static bool
check_sorted(int a[], int n) 
{
    int i;
    for (i = 0; i < n-1; i++)
        if (a[i] > a[i+1])
            return false;
    return true;
}

static int cmp_int(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

UNUSED static void builtin_qsort(int *a, int N)
{
    qsort(a, N, sizeof(int), cmp_int);
}

static inline void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

static int 
qsort_partition(int * array, int left, int right)
{
    int middle = left + (right-left)/2;

    // left <=> middle
    swap(array + left, array + middle);

    int current, last = left;
    for (current = left + 1; current <= right; ++current) {
        if (array[current] < array[left]) {
            ++last;
            // last <=> current
            swap(array + last, array + current);
        }
    }

    // left <=> last
    swap(array + left, array + last);
    return last;
}

static void 
qsort_internal_serial(int *array, int left, int right) 
{
    if (left >= right)
        return;

    int split = qsort_partition(array, left, right);
    qsort_internal_serial(array, left, split - 1);
    qsort_internal_serial(array, split + 1, right);
}

UNUSED static void 
qsort_serial(int *array, int N) 
{
    qsort_internal_serial(array, 0, N - 1);
}

struct qsort_task {
    int *array;
    int left, right, depth;
}; 

static int  
qsort_internal_parallel(struct thread_pool * threadpool, struct qsort_task * s)
{
    int * array = s->array;
    int left = s->left;
    int right = s->right;
    int depth = s->depth;

    if (left >= right)
        return 0;

    int split = qsort_partition(array, left, right);
    if (depth < 1) {
        qsort_internal_serial(array, left, split - 1);
        qsort_internal_serial(array, split + 1, right);
    } else {
        struct qsort_task qleft = {
            .left = s->left,
            .right = split-1,
            .depth = s->depth-1,
            .array = s->array
        };
        struct future * lhalf = thread_pool_submit(threadpool, 
                                   (fork_join_task_t)(void *)qsort_internal_parallel,  
                                   &qleft);
        struct qsort_task qright = {
            .left = split+1,
            .right = s->right,
            .depth = s->depth-1,
            .array = s->array
        };
        qsort_internal_parallel(threadpool, &qright);
        future_get(lhalf);
        future_free(lhalf);
    }
    return right - left;
}

static void 
qsort_parallel(int *array, int N) 
{
    struct qsort_task root = {
        .left = 0, .right = N-1, .depth = depth, .array = array
    };

    struct thread_pool * threadpool = thread_pool_new(nthreads);
    struct future * top = thread_pool_submit(threadpool,
                                             (fork_join_task_t)(void *)qsort_internal_parallel,
                                             &root);
    future_get(top);
    future_free(top);
    thread_pool_shutdown_and_destroy(threadpool);
}

static void 
benchmark(const char *benchmark_name UNUSED, sort_func sorter, int *a0, int N, bool report UNUSED)
{
    int *a = _mm_malloc(N * sizeof(int), mem_lock);
    memcpy(a, a0, N * sizeof(int));

    // parallel section here, including thread pool startup and shutdown
    sorter(a, N);

    // consistency check
    if (!check_sorted(a, N)) {
        printf("failed\n");
    }
    else{
        printf("success\n");
    }

    _mm_free(a, mem_lock);
}



void
test_main (void) 
{
  mm_init(mymemory,  128 * 1024 * 1024);
  pthread_mutex_init(&mem_lock);

    random_init(seed);

    int i, * a0 = _mm_malloc(N * sizeof(int), mem_lock);
    for (i = 0; i < N; i++){
        a0[i] = random_ulong();
        //printf("%u\n", a0[i]);    
    }

    benchmark("qsort parallel", qsort_parallel, a0, N, true);

  pthread_mutex_destroy(&mem_lock);
}