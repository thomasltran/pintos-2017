/*
 * Test that thread_foreach can cycle through all
 * the threads, not just the one on the current CPU.
 */
#include "threads/thread.h"
#include "threads/interrupt.h"
#include <debug.h>
#include <stdio.h>
#include "threads/cpu.h"
#include "tests/misc/misc_tests.h"
#include "devices/timer.h"
#include "lib/atomic-ops.h"

static int exit_flag = 0;

static void
each_func (struct thread *t, void *aux UNUSED)
{
  printf ("Thread %s has TID %d, on CPU %d\n", t->name, t->tid, t->cpu->id);
}

static void
threads_func (void *aux UNUSED)
{
  /* Spin so that thread_foreach is sure to see the thread */
  while (!exit_flag)
    ;
}

void
test_all_list (void)
{
  failIfFalse (ncpu >= 2, "Please run with a system with at least 2 CPUs");
  thread_create ("t1", 0, threads_func, 0);
  thread_create ("t2", 0, threads_func, 0);
  thread_create ("t3", 0, threads_func, 0);
  thread_create ("t4", 0, threads_func, 0);

  //Sleep to give AP's initial threads time to exit
  timer_sleep (2);
  thread_foreach (each_func, 0);
  printf ("==================================\n");
  atomic_store (&exit_flag, 1);
  //Hacky, but too lazy to make it better
  timer_sleep (5);
  /* Make sure exited threads are removed from all list */
  thread_foreach (each_func, 0);
  pass ();
}
