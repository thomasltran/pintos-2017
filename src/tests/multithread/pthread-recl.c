/* Spawns multiple pthreads, retrieves it's return value */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

#define NUM_THREADS 32

void * thread_func(void *);

void * thread_func(void *arg)
{
  return arg;
}

void
test_main (void) 
{
  int tids[NUM_THREADS];

  for(int c = 0; c < 50; c++)
  {
    for (int i = 0; i < NUM_THREADS; i++)
    {
      tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
      if(tids[i] == -1)
      {
        msg("ERROR ERORR");
      }
    }

    for (int i = 0; i < NUM_THREADS; i++)
    {
      void *ret;
      int join = pthread_join(tids[i], &ret);
      if(join == -1){
        msg("ERROR ERORR");
      }
      else{
        //msg("result %d", (int)(uintptr_t)ret);
      }
    }
      msg("count %d", c);
  }

}