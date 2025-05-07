/* Spawns 33 pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"

#define NUM_THREADS 33

void * thread_func(void *);

void * thread_func(void *arg)
{
  for(int i = 0; i<100000000; i+=2)
  {
    i--;
  }
  return (void *)(uintptr_t)arg;
}

void
test_main (void) 
{
  int tids[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++)
  {
    tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
    if(tids[i] == -1){
      msg("thread limit reached at %d", i);
    }
  }

  for (int i = 0; i < NUM_THREADS; i++)
  {
    void *ret;
    int join = pthread_join(tids[i], &ret);
    if(join == -1)
    {
      msg("invalid join at %d", i);
    }
    else{
      msg("result %d", (int)(uintptr_t)ret);
    }
  }
}