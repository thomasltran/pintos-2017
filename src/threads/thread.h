#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "filesys/file.h"
#include "threads/synch.h"
#include <stdbool.h>
#include "lib/kernel/bitmap.h"

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
  struct cpu *cpu; /* Points to the CPU this thread is currently bound to.
                      thread_unblock () will add a thread to the rq of
                      this CPU.  A load balancer needs to update this
                      field when migrating threads.
                    */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */
  struct list_elem sleepelem; /* List element for sleeping list */
  struct list_elem allelem; /* List element for all threads list. */

  int nice; /* Nice value. */
  int64_t wakeup;           /* Wakeup time for a sleeping thread */
  uint64_t vruntime; // vruntime of the thread
  uint64_t last_cpu_time;  // track start (running) time
  
#ifdef USERPROG
  struct parent_child * parent_child; // reference a child thread holds to its struct process
  struct list parent_child_list; // list of processes

  struct pcb * pcb;
  struct pthread_args * pthread_args; // per main/pthread
#endif
  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

// organize to program
#ifdef USERPROG
struct parent_child // struct to manage parent/child threads in process.c
{
   struct list_elem elem; // list of processes
   struct lock parent_child_lock; // protect process fields
   struct semaphore user_prog_exit; // child thread exit
   struct semaphore child_started; // child thread start (goes along with good_start)
   bool good_start; // started child process's thread and successfully making it past load
   int exit_status; // exit status of program
   int ref_count; // init to 2, decremented by parent in process_wait (and possibly exit if it didn't wait for its child) and child in process_exit
   tid_t child_tid; // thread id of child
   char * user_prog_name; // program name
   struct file * exe_file; // keep exe around until exit
};

struct pcb {
   struct lock lock; // todo: think
   bool multithread; // related to pthreads
   struct file **fd_table; /* file descriptor table */
   uint32_t *pagedir; /* Page directory. */
   struct bitmap * bitmap; // 0-31 for pthread tid

   struct list list;
};

struct pthread_args
{
   struct list_elem elem;

   // start_thread
   void (*wrapper)(void *, void *);
   void *esp;
   size_t pthread_tid;

   // save for later
   struct pcb *pcb;
   uint8_t *kpage;
   void * res;

   // synch
   struct semaphore pthread_exit; // exit/join
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
