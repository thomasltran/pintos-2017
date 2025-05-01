/* Tests argument passing to child processes. */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>
#include "lib/user/mm.h"
#include <string.h>


char mymemory[32 * 1024 * 1024]; // set chunk of memory

void * thread_func(void *);
static pthread_mutex_t lock;
static int counter = 0;

void *thread_func(void *arg)
{
  // set_tls(data, arg);

  // void * tls = get_tls(data);

  uintptr_t var = (uintptr_t)arg;
  int num = (int)var;
  char *test = _mm_malloc(5, lock);
  snprintf(test, 5, "hi%d", num);
  set_tls(0, test);

  // set_tls(5, (void*)var);
  // printf("%d\n", (int)(uintptr_t)get_tls(5));

  void * res = get_tls(0);
  printf("%s\n", (char*)res);



  _mm_free(test, lock);
  // _mm_free(test2, lock);

  return arg;
}

void
test_main (void) 
{
  mm_init(mymemory,  32 * 1024 * 1024);

  pthread_mutex_init(&lock);

  uint32_t tids[30];
  sem_t sem;
  sem_init(&sem, 1);

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
  void *res = get_tls(0);
  if(res == NULL){
    printf("tls NULL in main\n");
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


/*

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

// Structure to pass arguments to the worker threads
typedef struct {
    int thread_id;
    int start_range;
    int end_range;
    int* result_count;
    pthread_mutex_t* mutex;
} thread_args_t;

// CPU-intensive task: Find prime numbers in a given range
void* find_primes(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;
    int local_count = 0;
    
    for (int num = args->start_range; num <= args->end_range; num++) {
        if (num <= 1) continue;
        
        int is_prime = 1;
        for (int i = 2; i * i <= num; i++) {
            if (num % i == 0) {
                is_prime = 0;
                break;
            }
        }
        
        if (is_prime) {
            local_count++;
        }
    }
    
    // Update the global counter with a mutex to prevent race conditions
    pthread_mutex_lock(args->mutex);
    *(args->result_count) += local_count;
    pthread_mutex_unlock(args->mutex);
    
    printf("Thread %d found %d primes in range %d to %d\n", 
           args->thread_id, local_count, args->start_range, args->end_range);
    
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <number_of_threads> <range_end>\n", argv[0]);
        return 1;
    }
    
    int num_threads = atoi(argv[1]);
    int range_end = atoi(argv[2]);
    
    if (num_threads <= 0 || range_end <= 0) {
        printf("Both number of threads and range end must be positive integers\n");
        return 1;
    }
    
    printf("Starting benchmark with %d threads, finding primes up to %d\n", 
           num_threads, range_end);
    
    // Initialize mutex for thread synchronization
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    
    // Array to hold thread identifiers
    pthread_t* threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    
    // Array to hold thread arguments
    thread_args_t* thread_args = (thread_args_t*)malloc(num_threads * sizeof(thread_args_t));
    
    // Variable to store the total count of primes found
    int total_primes = 0;
    
    // Calculate workload distribution
    int chunk_size = range_end / num_threads;
    
    // Create and launch threads
    for (int i = 0; i < num_threads; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].start_range = i * chunk_size + 1;
        
        // Make sure the last thread covers any remaining elements
        if (i == num_threads - 1) {
            thread_args[i].end_range = range_end;
        } else {
            thread_args[i].end_range = (i + 1) * chunk_size;
        }
        
        thread_args[i].result_count = &total_primes;
        thread_args[i].mutex = &mutex;
        
        pthread_create(&threads[i], NULL, find_primes, &thread_args[i]);
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Total primes found: %d\n", total_primes);
    
    // Clean up
    pthread_mutex_destroy(&mutex);
    free(threads);
    free(thread_args);
    
    return 0;
}
*/


/*


*/