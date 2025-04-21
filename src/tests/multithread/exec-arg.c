/* Tests argument passing to child processes. */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>

void * thread_func(void *);

void * thread_func(void * arg){
  printf("child thread\n");
  uintptr_t y = (uintptr_t)arg;
  int z = (int)y;
  printf("child thread cast %d\n", z);
  //pthread_exit(0);
  while(true);
  return NULL;
}

void
test_main (void) 
{
  uintptr_t x = (uintptr_t)((int)42);

  printf("parent thread %d\n", (int)x);
  _pthread_create(thread_func, (void*)x);
  printf("parent thread done waiting\n");
  while(true);
  // pthread_join(thread, NULL);
  printf("parent thread done waiting\n");
}


/*
void * thread_func(void * arg){
    uintptr_t y = (uintptr_t)arg;
    int z = (int)y;

    printf("child thread %d\n", z);
    pthread_exit(0);
}

int main(){
    uintptr_t x = (uintptr_t)42;
    printf("parent thread\n");
    pthread_t thread;
    pthread_create(&thread, NULL, thread_func, (void*)x);
    pthread_join(thread, NULL);
    printf("parent thread done waiting\n");
}
*/
