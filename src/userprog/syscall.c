#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/cpu.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

static void syscall_handler (struct intr_frame *);
static void write(int fd, const void * buffer, unsigned size);
static void exit(int status);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f)
{
  //printf ("system call!\n");
  if(f->esp >= PHYS_BASE){
    printf("exceed valid\n");
    // need more checks
    return;
  }
  uint32_t sc_num = *((uint32_t *)(f->esp));
  if(sc_num == SYS_WRITE){
    int fd = *((int *)(f->esp + 4));
    const void *buffer = *((void **)(f->esp + 8));
    unsigned size = *((unsigned *)(f->esp + 12));
    write(fd, buffer, size);
  }
  else if(sc_num == SYS_EXIT){
    int status = *((int *)(f->esp + 4));
    f->eax = status;
    exit(status);
  }
}

// int write (int fd, const void *buffer, unsigned size)
// Writes size bytes from buffer to the open file fd. Returns the number of bytes actually written, which may be less than size if some bytes could not be written.

static void write(int fd, const void * buffer, unsigned size){
  lock_acquire(&fs_lock);
  putbuf(buffer, size);
  lock_release(&fs_lock);
  // how to write to a fd
}

/*
Whenever a user process terminates, because it called exit or for any other reason, print the process's name and exit code, formatted as if printed by printf ("%s: exit(%d)\n", ...);. The name printed should be the full name passed to (), omitting command-line arguments. Do not print these messages when a kernel thread that is not a user process terminates, or when the halt system call is invoked. The message is optional when a process fails to load.
*/

static void exit(int status){
  struct thread *thread_curr = thread_current(); // what if thread gets preempted here? best way to get the prog name—use stack?? 
  struct process * ps = thread_curr->ps;
  // not sure if we need the locking/disable intr
  
  printf("%s: exit(%d)\n", thread_curr->ps->user_prog_name, status);
  ps->exit_status = status;
  free(thread_curr->ps->user_prog_name);
  thread_exit();
}