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

/* When to switch from parallel to serial */
#define SERIAL_MERGE_SORT_THRESHOLD    1000
static int min_task_size = SERIAL_MERGE_SORT_THRESHOLD;

#define INSERTION_SORT_THRESHOLD    16
static int insertion_sort_threshold = INSERTION_SORT_THRESHOLD;

static int nthreads = NUM_THREADS;
static int seed = 44;
static int N = 3000000;

typedef void (*sort_func)(int *, int);

/* Return true if array 'a' is sorted. */
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

static void insertionsort(int *a, int lo, int hi) 
{
    int i;
    for (i = lo+1; i <= hi; i++) {
        int j = i;
        int t = a[j];
        while (j > lo && t < a[j - 1]) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = t;
    }
}

static void
merge(int * a, int * b, int bstart, int left, int m, int right)
{
    if (a[m] <= a[m+1])
        return;

    memcpy(b + bstart, a + left, (m - left + 1) * sizeof (a[0]));
    int i = bstart;
    int j = m + 1;
    int k = left;

    while (k < j && j <= right) {
        if (b[i] < a[j])
            a[k++] = b[i++];
        else
            a[k++] = a[j++];
    }
    memcpy(a + k, b + i, (j - k) * sizeof (a[0]));
}

static void
mergesort_internal(int * array, int * tmp, int left, int right)
{
    if (right - left < insertion_sort_threshold) {
        insertionsort(array, left, right);
    } else {
        int m = (left + right) / 2;
        mergesort_internal(array, tmp, left, m);
        mergesort_internal(array, tmp, m + 1, right);
        merge(array, tmp, 0, left, m, right);
    }
}

UNUSED static void
mergesort_serial(int * array, int n)
{
    if (n < insertion_sort_threshold) {
        insertionsort(array, 0, n);
    } else {
        int * tmp = _mm_malloc(sizeof(int) * (n / 2 + 1), mem_lock);
        mergesort_internal(array, tmp, 0, n-1);
        _mm_free (tmp, mem_lock);
    }
}

struct msort_task {
    int *array;
    int *tmp;
    int left, right;
}; 

static void  
mergesort_internal_parallel(struct thread_pool * threadpool, struct msort_task * s)
{
    int * array = s->array;
    int * tmp = s->tmp;
    int left = s->left;
    int right = s->right;

    if (right - left <= min_task_size) {
        mergesort_internal(array, tmp + left, left, right);
    } else {
        int m = (left + right) / 2;

        struct msort_task mleft = {
            .left = left,
            .right = m,
            .array = array,
            .tmp = tmp
        };
        struct future * lhalf = thread_pool_submit(threadpool, 
                                   (fork_join_task_t)(void *)mergesort_internal_parallel,  
                                   &mleft);

        struct msort_task mright = {
            .left = m + 1,
            .right = right,
            .array = array,
            .tmp = tmp
        };
        mergesort_internal_parallel(threadpool, &mright);
        future_get(lhalf);
        future_free(lhalf);
        merge(array, tmp, left, left, m, right);
    }
}

static void 
mergesort_parallel(int *array, int N) 
{
    int * tmp = _mm_malloc(sizeof(int) * (N), mem_lock);
    struct msort_task root = {
        .left = 0, .right = N-1, .array = array, .tmp = tmp
    };

    struct thread_pool * threadpool = thread_pool_new(nthreads);
    struct future * top = thread_pool_submit(threadpool,
                                             (fork_join_task_t)(void *)mergesort_internal_parallel,
                                             &root);
    future_get(top);
    future_free(top);
    thread_pool_shutdown_and_destroy(threadpool);
    _mm_free (tmp, mem_lock);
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
        printf("Sort failed\n");
    }else{
    printf("result ok.\n");
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
        printf("Using %d threads, parallel/serials threshold=%d insertion sort threshold=%d\n", 
        nthreads, min_task_size, insertion_sort_threshold);
    benchmark("mergesort parallel", mergesort_parallel, a0, N, true);

  pthread_mutex_destroy(&mem_lock);
}
// ./mergesort -n 16 -s 44 3000000