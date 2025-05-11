#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include "lib/user/threadpool.h"
#include "lib/user/mm.h"
#include <stdio.h>

#define NUM_THREADS 32

char mymemory[256 * 1024 * 1024]; // set chunk of memory
pthread_mutex_t mem_lock;

void * thread_func(void *);

void * thread_func(void *arg)
{
  return arg;
}

void
test_main (void) 
{
  mm_init(mymemory,  256 * 1024 * 1024);
  pthread_mutex_init(&mem_lock);

  // struct thread_pool * pool = thread_pool_new(NUM_THREADS);
  // thread_pool_shutdown_and_destroy(pool);

  pthread_mutex_destroy(&mem_lock);
}