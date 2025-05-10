/* Spawns multiple pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

#define NUM_THREADS 5
pthread_mutex_t counter_lock;

int counter = 0;

void * thread_func(void *);

void * thread_func(void *arg)
{
  pthread_mutex_lock(&counter_lock);
  msg("counter: %d", counter);
  counter +=1;
  pthread_mutex_unlock(&counter_lock);

  return arg;
}

void
test_main (void) 
{
  int tids[NUM_THREADS];
  pthread_mutex_init(&counter_lock);
  for (int i = 0; i < NUM_THREADS; i++)
  {
    tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
  }

  for (int i = 0; i < NUM_THREADS; i++)
  {
    void *ret;
    int join = pthread_join(tids[i], &ret);
    if(join == -1){
      msg("ERROR: JOIN FAILED");
    }
  }
  pthread_mutex_destroy(&counter_lock);
  msg("end");
  exit(0);
}