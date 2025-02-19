#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "filesys/file.h"
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING,       /* Running thread. */
  THREAD_READY,         /* Not running but ready to run. */
  THREAD_BLOCKED,       /* Waiting for an event to trigger. */
  THREAD_DYING          /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */
#define THREAD_NAME_MAX 16
/* Thread priorities. */
#define NICE_MIN -20                    /* Highest priority. */
#define NICE_DEFAULT 0                  /* Default priority. */
#define NICE_MAX 19                     /* Lowest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */

struct thread
{
  /* Owned by thread.c. */
  tid_t tid; /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[THREAD_NAME_MAX]; /* Name (for debugging purposes). */
  uint8_t *stack; /* Saved stack pointer. */
  int nice; /* Nice value. */
  int64_t wakeup;           /* Wakeup time for a sleeping thread */
  struct list_elem allelem; /* List element for all threads list. */

  struct cpu *cpu; /* Points to the CPU this thread is currently bound to.
                      thread_unblock () will add a thread to the rq of
                      this CPU.  A load balancer needs to update this
                      field when migrating threads.
                    */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */
  struct list_elem sleepelem; /* List element for sleeping list */

  uint64_t vruntime; // vruntime of the thread
  uint64_t last_cpu_time;  // track start (running) time

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */
  struct process * ps;
  struct list ps_list;
  struct lock ps_lock;
#endif
  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

#ifdef USERPROG
struct process
{
   struct list_elem elem; // list of processes
   int exit_status;
   int ref_count;
   tid_t child_tid;
   struct semaphore user_prog_exit;
   char * user_prog_name; // for exit
};
#endif

void thread_init(void);
void thread_init_on_ap (void);
void thread_start_idle_thread (void);
void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (struct spinlock *);
void thread_unblock (struct thread *);
struct thread *running_thread (void);
struct thread * thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_exit_ap (void) NO_RETURN;
/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);
int thread_get_nice (void);
void thread_set_nice (int);

#endif /* threads/thread.h */
