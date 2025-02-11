#include "threads/scheduler.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "list.h"
#include "threads/spinlock.h"
#include <debug.h>
#include "devices/timer.h"
/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
#define max(x, y) (((x) > (y)) ? (x) : (y)) /* Returns the larger of two values x and y. */

/*
 * This is a Completely Fair Scheduler (CFS) implementation with load balancing.
 *
 * Threads are kept in a ready queue ordered by their virtual runtime (vruntime).
 * The thread with the lowest vruntime is picked for scheduling.
 * Each thread's vruntime increases based on its actual runtime and nice value.
 * Upon preemption, the current thread's vruntime is updated and it is inserted
 * back into the ready queue based on its new vruntime.
 *
 * Load balancing occurs when CPUs are idle. The idle CPU will attempt to
 * "steal" threads from the busiest CPU's ready queue if the load imbalance
 * exceeds a threshold. When migrating threads between CPUs, their vruntimes
 * are adjusted relative to the min_vruntime of both queues to maintain fairness.
 *
 * Preemption occurs when a thread exceeds its calculated ideal runtime,
 * which is based on the total weight of all runnable threads.
 */

/* Called from thread_init () and thread_init_on_ap ().
   Initializes data structures used by the scheduler. 
 
   This function is called very early.  It is unsafe to call
   thread_current () at this point.
 */
void
sched_init (struct ready_queue *curr_rq)
{
  list_init (&curr_rq->ready_list);  
}

/* Priority to weight mapping table used by CFS scheduler.
 * Maps nice values (-20 to +19) to weights.
 * Higher weight means more CPU time.
 * Based on the Linux kernel prio_to_weight[] table.
 */
static const uint32_t prio_to_weight[40] =
  {
    /* -20 */    88761, 71755, 56483, 46273, 36291,
    /* -15 */    29154, 23254, 18705, 14949, 11916,
    /* -10 */    9548, 7620, 6100, 4904, 3906,
    /*  -5 */    3121, 2501, 1991, 1586, 1277,
    /*   0 */    1024, 820, 655, 526, 423,
    /*   5 */    335, 272, 215, 172, 137,
    /*  10 */    110, 87, 70, 56, 45,
    /*  15 */    36, 29, 23, 18, 15,
  };

/* Function declarations for CFS scheduler operations */
static uint64_t update_min_vruntime(struct ready_queue *rq, uint64_t current);
static bool vruntime_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static uint64_t update_total_weight(struct ready_queue *curr_rq, struct thread *current);
static uint64_t ideal_time(struct ready_queue *curr_rq, struct thread *current);
static uint64_t additional_vruntime(struct thread *current);

/* Called from thread.c:wake_up_new_thread () and
   thread_unblock () with the current CPU's ready queue
   locked (and preemption disabled).
   rq is the ready queue that "t" should be added to when
   it is awoken. It is not necessarily the current CPU.

   If called from wake_up_new_thread (), initial will be 1.
   If called from thread_unblock (), initial will be 0.

   If called from the idle thread, curr will be NULL.

   Returns RETURN_YIELD if the CPU containing rq should
   be rescheduled when this function returns, else returns
   RETURN_NONE */
enum sched_return_action sched_unblock(struct ready_queue *rq_to_add, struct thread *t, int initial, struct thread *curr)
{
  /* Initialize current thread's vruntime to maximum possible value.
   * This ensures proper comparison when checking if we need to yield CPU.
   * Will be updated with actual vruntime if current thread exists. */
  uint64_t curr_thread_vruntime = UINT64_MAX;

  if (curr != NULL)
  {
    /* Get current thread's vruntime */
    curr_thread_vruntime = curr->vruntime;
    /* Add additional vruntime to current thread's vruntime */
    curr_thread_vruntime += additional_vruntime(curr);
    /* Update minimum vruntime for the ready queue */
    rq_to_add->min_vruntime = update_min_vruntime(rq_to_add, curr_thread_vruntime);
  }

  /* If initial thread, set its vruntime to minimum vruntime */
  if (initial)
  {
    t->vruntime = rq_to_add->min_vruntime;
  }
  else
  {
    if (rq_to_add->min_vruntime > 20000000) // negative check
    {
      /* Ensure thread's vruntime is not less than minimum vruntime */
      t->vruntime = max(t->vruntime, rq_to_add->min_vruntime - 20000000);
    }
  }
  /* Insert thread into ready queue, following vruntime order policy */
  list_insert_ordered(&rq_to_add->ready_list, &t->elem, vruntime_less, NULL);
  rq_to_add->nr_ready++;

  /* CPU is idle or thread has lower vruntime than current thread */
  if (!curr || t->vruntime < curr_thread_vruntime)
  {
    return RETURN_YIELD;
  }
  /* No need to yield */
  return RETURN_NONE;
}

/* Called from thread_yield ().
 * Current thread is about to yield. Add it to the ready list
 *
 * Current ready queue is locked upon entry.
 */
void
sched_yield (struct ready_queue *curr_rq, struct thread *current)
{
  uint64_t running_vruntime = additional_vruntime(current);
  current->vruntime += running_vruntime;  
  /* Insert the current thread into ready queue,
     following the vruntime order policy. */
  list_insert_ordered(&curr_rq->ready_list, &current->elem, vruntime_less, NULL);
  curr_rq->nr_ready ++;
}

/* Called from next_thread_to_run ().
 * Find the next thread to run and remove it from the ready list
 * Return NULL if the ready list is empty.
 *
 * If the thread returned is different from the thread currently
 * running, a context switch will take place.
 *
 * Called with current ready queue locked.
 */
struct thread *
sched_pick_next (struct ready_queue *curr_rq)
{
  if (list_empty (&curr_rq->ready_list))
    return NULL;

  struct thread *ret = list_entry(list_pop_front (&curr_rq->ready_list), struct thread, elem);
  curr_rq->nr_ready--;
  ret->last_cpu_time = timer_gettime();
  return ret;
}

/* Iterates through the ready list and the current thread to find the minimum vruntime. 
 * This is important for maintaining the fairness of the scheduler.
 */
static uint64_t update_min_vruntime(struct ready_queue *rq, uint64_t current)
{
  uint64_t min_vruntime = UINT64_MAX;
  bool valid_min_vruntime = false;
  uint64_t curr_vruntime;

  if (!list_empty(&rq->ready_list))
  {
    for (struct list_elem *e = list_begin(&rq->ready_list); e != list_end(&rq->ready_list); e = list_next(e))
    {
      curr_vruntime = list_entry(e, struct thread, elem)->vruntime;
      if ((min_vruntime > curr_vruntime))
      {
        min_vruntime = curr_vruntime;
        valid_min_vruntime = true;
      }
    }
  }

  if (current != UINT64_MAX)
  {
    curr_vruntime = current;
    if ((min_vruntime > curr_vruntime))
    {
      min_vruntime = curr_vruntime;
      valid_min_vruntime = true;
    }
  }

  if(valid_min_vruntime && rq->min_vruntime < min_vruntime){
    return min_vruntime;
  }
  return rq->min_vruntime;
}

/* Compares two threads based on their vruntime.
 *
 * If the vruntime is the same, the thread with the lower tid is chosen.
 * Returns true if thread "a" has lower vruntime than thread "b".
 *
 * Used to order the threads in the ready list.
 */
static bool vruntime_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread* thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);
  if(thread_a->vruntime == thread_b->vruntime){
    return thread_a->tid < thread_b->tid;
  }
  return thread_a->vruntime < thread_b->vruntime;
}

/* Updates the total weight of the ready queue plus the current thread's weight. */
static uint64_t update_total_weight(struct ready_queue *curr_rq, struct thread *current)
{
  uint64_t total_ready_weight = 0;
  /* Sum the weights of all threads in the ready queue */
  for (struct list_elem *e = list_begin(&curr_rq->ready_list);
       e != list_end(&curr_rq->ready_list); e = list_next(e))
  {
    total_ready_weight += prio_to_weight[list_entry(e, struct thread, elem)->nice + 20];
  }

  /* If the current thread is not NULL, add its weight to the total weight of the ready queue */
  if(current){
    total_ready_weight += prio_to_weight[current->nice + 20];
  }
  return total_ready_weight;
}

/* Calculates the ideal time for a thread to run. */
static uint64_t ideal_time(struct ready_queue *curr_rq, struct thread *current)
{
  uint64_t total_weight = update_total_weight(curr_rq, current);
  uint64_t curr_ideal_time = (4000000 * (curr_rq->nr_ready + 1) * (uint64_t)prio_to_weight[current->nice + 20]) / total_weight;
  return curr_ideal_time;
}

/* Calculates the additional vruntime for a thread. */
static uint64_t additional_vruntime(struct thread *current)
{
  uint64_t curr_time = timer_gettime();
  uint64_t delta = curr_time - current->last_cpu_time;
  uint64_t curr_running_vruntime = (delta * prio_to_weight[20])/prio_to_weight[current->nice+20];
  return curr_running_vruntime;
}

/* Called from thread_tick ().
 * Ready queue rq is locked upon entry.
 *
 * Check if the current thread has finished its timeslice,
 * arrange for its preemption if it did.
 *
 * Returns RETURN_YIELD if current thread should yield
 * when this function returns, else returns RETURN_NONE.
 */
enum sched_return_action
sched_tick (struct ready_queue *curr_rq, struct thread *current)
{
  /* Enforce preemption. */
  uint64_t curr_time = timer_gettime();
  uint64_t curr_ideal_time = ideal_time(curr_rq, current);
  if((curr_time - current->last_cpu_time) >= curr_ideal_time){
    return RETURN_YIELD;
  }
  return RETURN_NONE;
}

/* Called from thread_block (). The base scheduler does
 * not need to do anything here, but your scheduler may. 
 *
 * 'current' is the current thread, about to block.
 */
void sched_block(struct ready_queue *rq UNUSED, struct thread *current)
{
  uint64_t running_vruntime = additional_vruntime(current);
  current->vruntime += running_vruntime;
}

/* Called from idle ().
 *
 * Implements CFS load balancing.
 * Migrates threads from the busiest CPU's ready queue to the current CPU
 * when imbalance exceeds threshold, while preserving vruntime fairness.
 *   
 * Strategy:
 * 1. Identify busiest CPU using cpu_load metric (sum of ready thread weights)
 * 2. Calculate imbalance as (busiest_load - current_load) / 2
 * 3. Migrate threads if imbalance * 4 > busiest_load (per CFS threshold)
 * 4. Adjust migrated threads' vruntime relative to min_vruntime of both queues 
 */
void sched_load_balance(){
  if (!cpu_started_others) // check for cpus array valid
  {
    return;
  }

  struct cpu *my_cpu = get_cpu();
  int busiest_cpu_index = -1;
  uint64_t busiest_cpu_load = UINT64_MAX;
  uint64_t my_load = UINT64_MAX;

  for (unsigned int i = 0; i < ncpu; i++)
  {
    /* Safely acquire lock before accessing the ith CPU's cpu_load */
    spinlock_acquire(&cpus[i].rq.lock);
    uint64_t steal_weight = update_total_weight(&cpus[i].rq, NULL);
    /* Update busiest CPU if current CPU has higher load */
    if (busiest_cpu_load == UINT64_MAX || busiest_cpu_load < steal_weight)
    {
      busiest_cpu_load = steal_weight;
      busiest_cpu_index = i;
    }

    if (cpus[i].id == my_cpu->id) // update curr cpu weight
    {
      my_load = steal_weight;
    }

    /* Release lock after accessing cpu_load */
    spinlock_release(&cpus[i].rq.lock);
  }

  if (busiest_cpu_load <= my_load)
  { // negative check, or load equals (imbalance would be 0)
    return;
  }

  uint64_t imbalance = (busiest_cpu_load - my_load) / 2;

  if (imbalance * 4 < busiest_cpu_load) // small imbalance
  {
    return;
  }

  uint64_t agg_weight = 0;
  struct ready_queue *my_rq = &my_cpu->rq;
  struct ready_queue *steal_rq = &cpus[busiest_cpu_index].rq;
  ASSERT(my_rq != steal_rq)

  while (agg_weight < imbalance) // keep migrating from busiest CPU to CPU that initiated the load balancing until agg_weight equals or exeeds imbalance
  {
    // Dr. Back's Formula
    // acquire locks in consistent order to solve the AB BA deadlock problem
    struct spinlock *lock1 = &my_rq->lock < &steal_rq->lock ? &my_rq->lock : &steal_rq->lock;
    struct spinlock *lock2 = &my_rq->lock < &steal_rq->lock ? &steal_rq->lock : &my_rq->lock;

    spinlock_acquire(lock1);
    spinlock_acquire(lock2);

    if (list_empty(&steal_rq->ready_list)) // nothing to migrate
    {
      spinlock_release(lock1);
      spinlock_release(lock2);
      return;
    }

    struct list_elem *steal_elem = list_pop_back(&steal_rq->ready_list);
    struct thread *steal_thread = list_entry(steal_elem, struct thread, elem);
    agg_weight += prio_to_weight[steal_thread->nice + 20];
    ASSERT(steal_thread->vruntime + my_rq->min_vruntime >= cpus[busiest_cpu_index].rq.min_vruntime); // negative check
    steal_thread->vruntime = steal_thread->vruntime + my_rq->min_vruntime - cpus[busiest_cpu_index].rq.min_vruntime; // adjust vruntime
    list_insert_ordered(&my_rq->ready_list, steal_elem, vruntime_less, NULL);

    spinlock_release(lock1);
    spinlock_release(lock2);
  }
}