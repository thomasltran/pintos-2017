// ported from CS3214, test by Dr. Back

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include "lib/user/threadpool.h"
#include "lib/user/mm.h"
#include <stdio.h>

#define NUM_THREADS 32
static int len = 60000000;

char mymemory[256 * 1024 * 1024]; // set chunk of memory
pthread_mutex_t mem_lock;

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


void
test_main (void) 
{
  mm_init(mymemory,  256 * 1024 * 1024);
  pthread_mutex_init(&mem_lock);

  struct thread_pool * pool = thread_pool_new(NUM_THREADS);

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
}