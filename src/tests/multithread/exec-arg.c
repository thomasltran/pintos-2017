/* Tests argument passing to child processes. */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

void * thread_func(void *);

void *thread_func(void *arg)
{
  uintptr_t y = (uintptr_t)arg;
  // int z = (int)y;
  pthread_exit((void *)y);
  return NULL;
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
void * thread_func(void * arg){
  uintptr_t y = (uintptr_t)arg;
  int z = (int)y;

  printf("child thread %d\n", z);
  pthread_exit((void *)y);
}

int main(){
  uintptr_t x = (uintptr_t)42;
  printf("parent thread\n");

  pthread_t thread;
  void * ret;

  pthread_create(&thread, NULL, thread_func, (void*)x);
  pthread_join(thread, &ret);

  int result = (int)(uintptr_t)ret;
  printf("parent thread done waiting %d\n", result);

  return 0;
}
*/
