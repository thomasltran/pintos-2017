#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>
#include <debug.h>

/* Spinlock */
struct spinlock
{
  int locked;	        /* Is the lock held? */
  struct cpu *cpu;      /* CPU that acquired the lock, or NULL 
		           spinlock is not held */
  
  /* For debugging. If the lock is held, then debuginfo
     contains the call stack of the thread when the lock
     was acquired. If lock is not held, contains the
     call stack of the thread when it released the lock */
  struct callerinfo debuginfo;
};

void spinlock_acquire (struct spinlock*);
bool spinlock_try_acquire (struct spinlock *);
bool spinlock_held_by_current_cpu (const struct spinlock*);
void spinlock_init (struct spinlock*);
void spinlock_release (struct spinlock*);

/* A counting semaphore. */
struct semaphore 
  {
    unsigned value;             /* Current value. */
    struct list waiters;        /* List of waiting threads. */
    struct spinlock lock;
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock 
  {
    struct thread *holder;      /* Thread holding lock (for debugging). */
    struct semaphore semaphore; /* Binary semaphore controlling access. */
    struct callerinfo debuginfo;/* Debugging info. See explanation above */
  };

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition 
  {
    struct list waiters;        /* List of waiting threads. */
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
