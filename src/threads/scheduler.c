#include "threads/scheduler.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "list.h"
#include "threads/spinlock.h"
#include <debug.h>
#include "devices/timer.h"
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
   Initializes data structures used by the scheduler. 
 
   This function is called very early.  It is unsafe to call
   thread_current () at this point.
 */
void
sched_init (struct ready_queue *curr_rq)
{
  list_init (&curr_rq->ready_list);
  curr_rq->min_vruntime = 0;
  curr_rq->total_weight = 0;
}

static const uint32_t prio_to_weight[40] =
    {
        /* -20 */ 88761,
        71755,
        56483,
        46273,
        36291,
        /* -15 */ 29154,
        23254,
        18705,
        14949,
        11916,
        /* -10 */ 9548,
        7620,
        6100,
        4904,
        3906,
        /*  -5 */ 3121,
        2501,
        1991,
        1586,
        1277,
        /*   0 */ 1024,
        820,
        655,
        526,
        423,
        /*   5 */ 335,
        272,
        215,
        172,
        137,
        /*  10 */ 110,
        87,
        70,
        56,
        45,
        /*  15 */ 36,
        29,
        23,
        18,
        15,
};

/* Called from thread.c:wake_up_new_thread () and
   thread_unblock () with the CPU's ready queue locked.
   rq is the ready queue that t should be added to when
   it is awoken. It is not necessarily the current CPU.

   If called from wake_up_new_thread (), initial will be 1.
   If called from thread_unblock (), initial will be 0.

   Returns RETURN_YIELD if the CPU containing rq should
   be rescheduled when this function returns, else returns
   RETURN_NONE */
enum sched_return_action sched_unblock(struct ready_queue * rq_to_add, struct thread *t, int initial)
{
  // uint64_t blocked_cpu_time = calculate_cpu_time(t);

  //obtaining cpu_time of current thread and updating its vruntime
  uint64_t running_cpu_time = rq_to_add->curr ? calculate_cpu_time(rq_to_add->curr): 0;
  if(rq_to_add->curr){
    rq_to_add->curr->vruntime += running_cpu_time;
  }

  //obtaining min_vruntime
  calculate_min_vruntime(rq_to_add, rq_to_add->curr);
  uint64_t min_vruntime = rq_to_add->min_vruntime;
  
  if(initial){
    //setting vruntime_0 for newly created thread
    t->vruntime = min_vruntime;
  }
  else{
    //implementing sleeper bonus
    uint64_t higher_vruntime = min_vruntime-20000000;
    //finding max between vruntime of thread and min_vruntime with sleeper bonus
    if(t->vruntime > higher_vruntime){
      higher_vruntime = t->vruntime;
    }
    //setting vruntime_0 of existing unblocked thread
    t->vruntime = higher_vruntime;
    
    //comparing vruntime of unblocked and running thread
    if(rq_to_add->curr &&  t->vruntime < rq_to_add->curr->vruntime){
      //preempting running thread and putting unblocked I/O thread to front of ready_list
      list_push_front(&rq_to_add->ready_list, &t->elem);
      rq_to_add->nr_ready++;
      return RETURN_YIELD;
    }
  }

  /* CPU is idle */
  if (!rq_to_add->curr)
    {
      //putting unblocked I/O thread to front of ready_list
      list_push_front(&rq_to_add->ready_list, &t->elem);
      rq_to_add->nr_ready++;
      return RETURN_YIELD;
    }
  else{
    //inserting unblocked thread into ready queue
    list_insert_ordered (&rq_to_add->ready_list, &t->elem, vruntime_less, NULL);
    rq_to_add->nr_ready++;
  }

  rq_to_add->curr->vruntime -= running_cpu_time;

  return RETURN_NONE;
}

/* Called from thread_yield ().
   Current thread is about to yield.  Add it to the ready list

   Current ready queue is locked upon entry.
 */
void
sched_yield (struct ready_queue *curr_rq, struct thread *current)
{
  uint64_t cpu_time = calculate_cpu_time(current);
  current->vruntime += cpu_time;  
  list_insert_ordered(&curr_rq->ready_list, &current->elem, vruntime_less, NULL);
  curr_rq->nr_ready ++;
}

/* Called from next_thread_to_run ().
   Find the next thread to run and remove it from the ready list
   Return NULL if the ready list is empty.

   If the thread returned is different from the thread currently
   running, a context switch will take place.

   Called with current ready queue locked.
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

void calculate_min_vruntime(struct ready_queue * rq, struct thread* current){
  struct list* curr_rl = &rq->ready_list;
  uint64_t min_vruntime;
  bool valid_min_vruntime = false;
  uint64_t curr_vruntime;
  if(!list_empty(curr_rl)){
    struct list_elem * e = list_front(curr_rl);
    min_vruntime = list_entry(e, struct thread, elem)->vruntime;
    e = e->next;
    while(e != list_end(curr_rl)){
      struct list_elem* next_e = e->next;
      curr_vruntime = list_entry(e, struct thread, elem)->vruntime;
      if(!valid_min_vruntime || min_vruntime > curr_vruntime){
        min_vruntime = curr_vruntime;
        valid_min_vruntime= true;
      }
      e = next_e;
    }
  }

  if(current){
    curr_vruntime = current->vruntime;
    if(!valid_min_vruntime || min_vruntime > curr_vruntime){
      min_vruntime = curr_vruntime;
      valid_min_vruntime= true;

    }
  }
  if(valid_min_vruntime && rq->min_vruntime < min_vruntime){
    rq->min_vruntime = min_vruntime;
  }
}

bool vruntime_less(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED){

  struct thread* thread_a = list_entry(a, struct thread, elem);
  struct thread* thread_b = list_entry(b, struct thread, elem);
  return thread_a->vruntime < thread_b->vruntime;
}

void calculate_total_weight(struct ready_queue *curr_rq, struct thread *current)
{
  uint64_t total_weight = 0;
  struct list_elem *e = list_begin(&curr_rq->ready_list);
  while (e != list_end(&curr_rq->ready_list))
  {
    struct list_elem* nextE = e->next;
    total_weight += prio_to_weight[list_entry(e, struct thread, elem)->nice + 20];
    e = nextE;
  }

  if(current){
    total_weight += prio_to_weight[current->nice + 20];
  }
  curr_rq->total_weight = total_weight;
}

uint64_t calculate_ideal_time(struct ready_queue *curr_rq, struct thread *current)
{
  calculate_total_weight(curr_rq, current);
  uint64_t four_mil = 4000000;
  uint64_t number_of_threads = curr_rq->nr_ready + 1;
  uint64_t weight_of_current = prio_to_weight[current->nice + 20];
  uint64_t total_weight = curr_rq->total_weight;
  uint64_t ideal_time = (four_mil);
  ideal_time *= number_of_threads;
  ideal_time *=weight_of_current;
  ideal_time /= total_weight;
  // uint64_t ideal_time = (4000000 * ((curr_rq->nr_ready)+1) * prio_to_weight[current->nice + 20]) / curr_rq->total_weight;
  return ideal_time;
}

uint64_t calculate_cpu_time(struct thread* current){
  uint64_t curr_time = timer_gettime();
  uint64_t delta = curr_time - current->last_cpu_time;
  uint64_t cpu_time = (delta * prio_to_weight[20])/prio_to_weight[current->nice+20];
  return cpu_time;
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
  curr_rq->thread_ticks++;
  uint64_t ideal_time = calculate_ideal_time(curr_rq, current);
  uint64_t cpu_time = calculate_cpu_time(current);
  if(cpu_time >= ideal_time){
    return RETURN_YIELD;
  }
  return RETURN_NONE;
  

  // if (++curr_rq->thread_ticks >= TIME_SLICE)
  //   {
  //     /* Start a new time slice. */
  //     curr_rq->thread_ticks = 0;

  //     return RETURN_YIELD;
  //   }
  // return RETURN_NONE;
}

/* Called from thread_block (). The base scheduler does
   not need to do anything here, but your scheduler may. 

   'cur' is the current thread, about to block.
 */
void
sched_block (struct ready_queue *rq, struct thread *current)
{
  struct list_elem* e = list_begin(&rq->ready_list);
  while(e != list_end(&rq->ready_list)){
    struct list_elem* next_e = e->next;
    if(e == &current->elem){
      list_remove(&current->elem);
      rq->nr_ready--;
      break;
    }
    e = next_e;
  }
}
