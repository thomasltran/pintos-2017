/* Spawns multiple pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>
// #include 
#define NUM_THREADS 32


pthread_cond_t share_cond;
pthread_cond_t main_cond;

pthread_mutex_t share_lock;
int share_var;
void * thread_func(void *);

void * thread_func(void *arg)
{
  // msg("in thread %d", (int)(uintptr_t)arg);
  pthread_mutex_lock(&share_lock);
  pthread_cond_wait(&share_cond, &share_lock);
  msg("share_var: %d", share_var);
  pthread_cond_signal(&main_cond, &share_lock);
  pthread_mutex_unlock(&share_lock);
  return arg;
}

void
test_main (void) 
{
  pthread_cond_init(&share_cond);
  pthread_cond_init(&main_cond);
  pthread_mutex_init(&share_lock);
  int tids[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++)
  {
    tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
  }

  for(int i = 0; i < NUM_THREADS; i++){
    pthread_mutex_lock(&share_lock);
    share_var = i;
    pthread_cond_signal(&share_cond, &share_lock);
    pthread_cond_wait(&main_cond, &share_lock);
    pthread_mutex_unlock(&share_lock);

  }
  for (int i = 0; i < NUM_THREADS; i++)
  {
    void *ret;
    int join = pthread_join(tids[i], &ret);
    if(join == -1){
      msg("ERROR: JOIN FAILED");
    }
  }

  pthread_cond_destroy(&share_cond);
  pthread_mutex_destroy(&share_lock);
}