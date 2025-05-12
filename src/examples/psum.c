// ported from CS3214, test by Dr. Back

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include "lib/user/threadpool.h"
#include "lib/user/mm.h"
#include <stdio.h>

#include <stdlib.h>

#define NUM_THREADS 32

char mymemory[256 * 1024 * 1024]; // set chunk of memory
pthread_mutex_t mem_lock;

static int nthreads = NUM_THREADS;

struct problem_parameters {
    unsigned beg, end;
    int *v;
};

#define GRANULARITY 100

static void *
parallel_sum(struct thread_pool * pool, void * _data)
{
    struct problem_parameters * p = _data;
    unsigned int i, len = p->end - p->beg;
    if (len < GRANULARITY) {
        uintptr_t sum = 0;
        int * v = p->v;
        for (i = p->beg; i < p->end; i++)
            sum += v[i];
        return (void *) sum;   
    }
    int mid = p->beg + len / 2;

    struct problem_parameters left_half = {
        .beg = p->beg, .end = mid, .v = p->v
    };
    struct problem_parameters right_half = {
        .beg = mid, .end = p->end, .v = p->v
    };
    struct future * f = thread_pool_submit(pool, parallel_sum, &right_half);
    uintptr_t lresult = (uintptr_t) parallel_sum(pool, &left_half);
    uintptr_t rresult = (uintptr_t) future_get(f);
    future_free(f);
    return (void *)(lresult + rresult);
}

static void usage(char *av0, int nthreads)
{
    printf("Usage: %s [-n <n>] <N>\n"
           " -n        number of threads in pool, default %d\n",
           av0, nthreads);
    exit(0);
}

int main(int argc, char * argv[]){
    mm_init(mymemory, 256 * 1024 * 1024);
    pthread_mutex_init(&mem_lock);

    int len = 3000000; // len * sizeof int right below

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            switch (argv[i][1])
            {
            case 'n':
                nthreads = atoi(argv[++i]);
                break;
            case 'h':
                usage(argv[0], nthreads);
                break;
            default:
                usage(argv[0], nthreads);
                exit(0);
                break;
            }
        }
        else
        {
            len = atoi(argv[i]);
            if (len > 60000000)
            {
                printf("N must be less than 60000000\n");
                printf("%d * 4 > 256 MB\n", len * 4);
                exit(0);
            }
        }
    }

  struct thread_pool * pool = thread_pool_new(nthreads);

  int * v = _mm_malloc(sizeof(int) * len, mem_lock);
  struct problem_parameters roottask = {
      .beg = 0, .end = len, .v = v,
  };

  unsigned long long realsum = 0;
  int i;
  for (i = 0; i < len; i++) {
      v[i] = i % 3;
      realsum += v[i];
  }


  struct future *f = thread_pool_submit(pool, parallel_sum, &roottask);
  unsigned long long sum = (unsigned long long)(uintptr_t)future_get(f);

  future_free(f);

  if (sum != realsum) {
      printf("result %lld should be %lld\n", sum, realsum);
  } else {
      printf("result ok.\n");
  }

  thread_pool_shutdown_and_destroy(pool);

  pthread_mutex_destroy(&mem_lock);
  return 0;
}