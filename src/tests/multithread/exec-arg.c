/* Tests argument passing to child processes. */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"

void
test_main (void) 
{
  pthread_create(NULL, NULL);
}

/*
#include <pthread.h>
#include <stdio.h>

void * thread_func(void * arg){
    printf("child thread\n");
    pthread_exit(0);
}

int main(){
    printf("parent thread\n");
    pthread_t thread;
    pthread_create(&thread, NULL, thread_func, NULL);
    pthread_join(thread, NULL);
    printf("parent thread done waiting\n");
}
*/