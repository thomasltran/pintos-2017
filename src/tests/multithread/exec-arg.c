/* Tests argument passing to child processes. */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

void * thread_func(void *);
static pthread_mutex_t lock;
static int counter = 0;

void *thread_func(void *arg)
{
  return arg;
}

void
test_main (void) 
{
  uint32_t tids[30];

  for (int i = 0; i < 30; i++)
  {
    uintptr_t x = (uintptr_t)i;

    tids[i] = _pthread_create(thread_func, (void *)x);
    // printf("tid %d\n", tids[i]);
  }

  for (int i = 0; i < 30; i++)
  {
    void *ret;
    pthread_join(tids[i], &ret);
    int result = (int)(uintptr_t)ret;
    printf("join %d\n", result);
  }
}

// todo: have macro for tlimit, make sure slots gettin reused, error check for create if exceed and try again after, err check join, exit out of ord should work
/*
void *thread_func(void *arg)
{
  return arg;
}

void
test_main (void) 
{
  uint32_t tids[30];

  for (int i = 0; i < 30; i++)
  {
    uintptr_t x = (uintptr_t)i;

    tids[i] = _pthread_create(thread_func, (void *)x);
    // printf("tid %d\n", tids[i]);
  }

  for (int i = 0; i < 30; i++)
  {
    void *ret;
    pthread_join(tids[i], &ret);
    int result = (int)(uintptr_t)ret;
    printf("join %d\n", result);
  }
}
*/
