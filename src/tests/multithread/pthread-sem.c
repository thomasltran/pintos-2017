/* Spawns multiple pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

#define NUM_THREADS 32
sem_t pthread_sem;
sem_t main_sem;

void * thread_func(void *);

void * thread_func(void *arg)
{
  sem_down(&pthread_sem);
  sem_post(&main_sem);
  return arg;
}

void
test_main (void) 
{
  int tids[NUM_THREADS];
  // pthread_mutex_init(&counter_lock);
  sem_init(&pthread_sem, 0);
  msg("sem_init pthread_sem");
  sem_init(&main_sem, 0);
  msg("sem_init main_sem");
  for (int i = 0; i < NUM_THREADS; i++)
  {
    tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
    sem_post(&pthread_sem);
    msg("sem_post pthread_sem: %d", i);

  }
  for (int i = 0; i < NUM_THREADS; i++)
  {
    sem_down(&main_sem);
    msg("sem_down main_sem: %d", i);
    void *ret;
    int join = pthread_join(tids[i], &ret);
    if(join == -1){
      msg("ERROR: JOIN FAILED");
    }
  }
  sem_destroy(&pthread_sem);
  msg("sem_destroy pthread_sem");
  sem_destroy(&main_sem);
  msg("sem_destroy main_sem");

}