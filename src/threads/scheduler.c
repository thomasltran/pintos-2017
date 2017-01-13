#include "threads/scheduler.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "list.h"
#include "threads/synch.h"
#include <debug.h>

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */

/* Called from thread_init () and thread_AP_init ().
   Initializes data structures used by the scheduler. */
void
sched_init (struct rq *rq)
{
  list_init (&rq->rr.ready_list);
  spinlock_init (&rq->ready_spinlock);
}

/* Called from thread.c:wake_up_new_thread () and
   thread_unblock ().
   Thread is unblocked, either from a synchronization
   mechanism or because it was just created.
   If awoken from synchronization mechanism, initial 
   is 0, else initial is 1. */
void
sched_unblock (struct rq *rq, struct thread *t, int initial UNUSED)
{
  list_push_back (&rq->rr.ready_list, &t->elem);
  rq->rr.nr_running++;
}

/* Called from thread_yield ().
   Current thread is about to yield. Add it to the ready list */
void
sched_yield (struct rq *rq, struct thread *prev)
{
  list_push_back (&rq->rr.ready_list, &prev->elem);
  rq->rr.nr_running ++;
}

/* Called from next_thread_to_run ().
   Find the next thread to run and remove it from the ready list 
   Return NULL if no thread found. */
struct thread *
sched_pick_next (struct rq *rq)
{
  struct thread *ret = NULL;
  if (list_empty (&rq->rr.ready_list))
    goto out;
  ret = list_entry(list_pop_front (&rq->rr.ready_list), struct thread, elem);
  rq->rr.nr_running--;
  out: return ret;
}

/* Called from thread_tick ().
   Check if the current thread has finished its timeslice, and preempt
   if it did. */
void
sched_tick (struct rq *rq, struct thread *p UNUSED)
{
  /* Enforce preemption. */
  if (++rq->rr.thread_ticks >= TIME_SLICE)
    {
      intr_yield_on_return ();
      /* Start a new time slice */
      rq->rr.thread_ticks = 0;
    }
}

/* Called from thread_block (). The base scheduler does
   not do anything here */
void
sched_block (struct rq *rq UNUSED, struct thread *p UNUSED)
{
  ;
}
