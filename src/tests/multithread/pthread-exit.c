/* pthread exit with main thread */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"

void * thread_func(void *);

void * thread_func(void *arg)
{
  msg("pthread create %d", (int)(uintptr_t)arg);
  return (void *)(uintptr_t)21;
}

void
test_main (void) 
{
  int tid = _pthread_create(thread_func, (void *)(uintptr_t)42);

  void *ret;
  pthread_join(tid, &ret);

  msg("result %d", (int)(uintptr_t)ret);
  pthread_exit(NULL);
}