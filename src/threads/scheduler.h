#ifndef THREADS_SCHEDULER_H_
#define THREADS_SCHEDULER_H_

#include <stdint.h>
#include "threads/thread.h"
#include "threads/synch.h"

struct rr_rq
{
  unsigned thread_ticks;
  struct list ready_list;
  unsigned long nr_running;
};

struct rq
{
  struct rr_rq rr;
  struct spinlock ready_spinlock;
  int pulled;
  struct thread *curr;     // Pointer to current thread. NULL if idle
  struct thread *idle_thread;
};

void sched_init (struct rq *);
void sched_unblock (struct rq *, struct thread *, int );
void sched_yield (struct rq *, struct thread *);
struct thread *sched_pick_next (struct rq *);
void sched_tick (struct rq *, struct thread *);
void sched_block (struct rq *, struct thread *);

#endif /* THREADS_SCHEDULER_H_ */
