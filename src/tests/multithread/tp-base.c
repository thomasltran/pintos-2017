/* Demo with TLS, malloc */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>
#include "lib/user/mm.h"
#include <string.h>

#define NUM_THREADS 32
char mymemory[32 * 1024 * 1024]; // set chunk of memory
static pthread_mutex_t lock;     // malloc lock

void * thread_func(void *);
void *thread_func(void *arg)
{
  // allocates mem for a string and saves it in tls
  char *buff = _mm_malloc(5, lock);
  snprintf(buff, 5, "hi%d", (int)(uintptr_t)arg);
  set_tls(1, buff);

  // retrieve the string from tls
  void *res = get_tls(1);

  return res;
}

void
test_main (void) 
{
  // init chunk
  mm_init(mymemory, 32 * 1024 * 1024);
  pthread_mutex_init(&lock);

  uint32_t tids[NUM_THREADS]; // holds tids

  // pass in i
  for (int i = 0; i < NUM_THREADS; i++)
  {
    tids[i] = _pthread_create(thread_func, (void *)(uintptr_t)i);
  }

  // retrieves res of thread func
  for (int i = 0; i < NUM_THREADS; i++)
  {
    void *ret;
    pthread_join(tids[i], &ret);
    printf("%s\n", (char *)ret);
    _mm_free(ret, lock);
  }

  // never set tls in main thread
  void *res = get_tls(1);
  if (res == NULL)
  {
    printf("tls NULL in main\n");
  }
}