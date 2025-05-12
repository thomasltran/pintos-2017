/* Spawns multiple pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

#define NUM_THREADS 1
#define MAX 100000000
void * thread_func(void *);
bool prime_number_check(int);
int counters[NUM_THREADS];
// pthread_mutex_t counter_lock;

bool prime_number_check(int num){
    if(num == 1){
      return false;
    }
    if(num == 2 || num == 3 || num==5){
      return true;
    }
    if(num % 5 == 0 || num % 2 == 0|| num % 3 == 0){
      return false;   
    }
    for(int i = 7; i * i <= num; i+=2){
      if(num % i == 0){
        return false;
      }
    }
    return true;
}

void * thread_func(void *arg)
{
  int tid = (int)arg;
  int start = tid * MAX/NUM_THREADS;
  int end = start + MAX/NUM_THREADS;
  // msg("tid %d, start: %d, end: %d", (int)arg, start, end);

  for(int i = start; i < end; ++i){
    // msg("tid %d: %d", (int)(uintptr_t)arg, i);
    if(prime_number_check(i)){
      // pthread_mutex_lock(&counter_lock);
      ++counters[tid];
      // pthread_mutex_unlock(&counter_lock);
    }
  }
  return arg;
}

void
test_main (void) 
{
  int tids[NUM_THREADS];
  // pthread_mutex_init(&counter_lock);
  for (int i = 0; i < NUM_THREADS; i++)
  {
    counters[i] = 0;
    tids[i] = _pthread_create(thread_func, (void *)i);
  }

  int counter = 0;
  for (int i = 0; i < NUM_THREADS; i++)
  {
    void *ret;
    int join = pthread_join(tids[i], &ret);
    if(join == -1){
      msg("ERROR ERORR");
    }
    else{
      // msg("result %d", (int)(uintptr_t)ret);
      counter += counters[i];
    }
  }
  msg("counter: %d", counter);
}