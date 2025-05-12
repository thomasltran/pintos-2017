/* Test courtesy of Claude—just refactored for multithreading on our end */

#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include <stdio.h>
#include "lib/user/mm.h"
#include <string.h>


char mymemory[32 * 1024 * 1024]; // set chunk of memory

static pthread_mutex_t lock;
void* find_primes(void* arg);
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

void test_main (void)  {

  mm_init(mymemory, 32 * 1024 * 1024);
  int num_threads = 32;
  int range_end = 100000000;
  
  printf("Starting benchmark with %d threads, finding primes up to %d\n", 
         num_threads, range_end);
  
  // Initialize mutex for thread synchronization
  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex);

  uint32_t tids[num_threads];

  // Array to hold thread arguments
  thread_args_t* thread_args = (thread_args_t*)_mm_malloc(num_threads * sizeof(thread_args_t), lock);
  
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
      
      tids[i] = _pthread_create(find_primes, &thread_args[i]);
  }
  
  // Wait for all threads to complete
  for (int i = 0; i < num_threads; i++) {
      pthread_join(tids[i], NULL);
  }
  
  printf("Total primes found: %d\n", total_primes);
  
  // Clean up
  pthread_mutex_destroy(&mutex);
  _mm_free(thread_args, lock);
  
}