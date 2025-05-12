/* attempts to spawn 33 pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"

#define NUM_THREADS 33

static pthread_mutex_t cond_lock;
static pthread_cond_t cond;
static bool created = false;

void * thread_func(void *);

void * thread_func(void *arg)
{
  pthread_mutex_lock(&cond_lock);
  while(!created)
  {
    pthread_cond_wait(&cond, &cond_lock);
  }
  pthread_mutex_unlock(&cond_lock);
  return arg;
}

void
test_main (void) 
{
  int tids[NUM_THREADS];

  pthread_mutex_init(&cond_lock);
  pthread_cond_init(&cond);

  for (int i = 0; i < NUM_THREADS; i++)
  {
    tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
    if(tids[i] == -1){
      msg("thread limit reached at %d", i);
    }
  }

  pthread_mutex_lock(&cond_lock);
  created = true;
  pthread_cond_broadcast(&cond, &cond_lock);
  pthread_mutex_unlock(&cond_lock);

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

  pthread_mutex_destroy(&cond_lock);
  pthread_cond_destroy(&cond);
}