#include "threads/scheduler.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "list.h"
#include "threads/spinlock.h"
#include <debug.h>

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */

/*
 * In the provided baseline implementation, threads are kept in an unsorted list.
 *
 * Threads are added to the back of the list upon creation.
 * The thread at the front is picked for scheduling.
 * Upon preemption, the current thread is added to the end of the queue
 * (in sched_yield), creating a round-robin policy if multiple threads
 * are in the ready queue.
 * Preemption occurs every TIME_SLICE ticks.
 */

/* Called from thread_init () and thread_init_on_ap ().
   Initializes data structures used by the scheduler. */
void
sched_init (struct ready_queue *rq)
{
  list_init (&rq->ready_list);
}

/* Called from thread.c:wake_up_new_thread () and
   thread_unblock () with the current CPU's ready queue locked.

   Thread is unblocked, either from a synchronization
   mechanism or because it was just created.
   If awoken from synchronization mechanism, initial
   is 0, else initial is 1. */
void
sched_unblock (struct ready_queue *rq, struct thread *t, int initial UNUSED)
{
  list_push_back (&rq->ready_list, &t->elem);
  rq->nr_ready++;
}

/* Called from thread_yield ().
   Current thread is about to yield. Add it to the ready list

   Current ready queue is locked upon entry.
 */
void
sched_yield (struct ready_queue *rq, struct thread *t)
{
  list_push_back (&rq->ready_list, &t->elem);
  rq->nr_ready ++;
}

/* Called from next_thread_to_run ().
   Find the next thread to run and remove it from the ready list
   Return NULL if no thread found.

   Called with current ready queue locked.
 */
struct thread *
sched_pick_next (struct ready_queue *rq)
{
  if (list_empty (&rq->ready_list))
    return NULL;

  struct thread *ret = list_entry(list_pop_front (&rq->ready_list), struct thread, elem);
  rq->nr_ready--;
  return ret;
}

/* Called from thread_tick ().
 * Ready queue rq is locked upon entry.
 *
 * Check if the current thread has finished its timeslice, and preempt
 * if it did.
 */
void
sched_tick (struct ready_queue *rq, struct thread *current UNUSED)
{
  /* Enforce preemption. */
  if (++rq->thread_ticks >= TIME_SLICE)
    {
      intr_yield_on_return ();

      /* Start a new time slice. */
      rq->thread_ticks = 0;
    }
}

/* Called from thread_block (). The base scheduler does
   not need to do anything here, but your scheduler may. */
void
sched_block (struct ready_queue *rq UNUSED, struct thread *p UNUSED)
{
  ;
}
