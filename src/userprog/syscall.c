#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static void write(int fd, const void * buffer, unsigned size);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f)
{
  printf ("system call!\n");
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

  thread_exit ();
}

// int write (int fd, const void *buffer, unsigned size)
// Writes size bytes from buffer to the open file fd. Returns the number of bytes actually written, which may be less than size if some bytes could not be written.

static void write(int fd, const void * buffer, unsigned size){
  // putbuf (const char *buffer, size_t n)
  // if(fd == 1){
  //   putbuf(buffer, size);
  // }

  lock_acquire(&fs_lock);
  // printf("fd %d size %d\n", fd, size);
  putbuf(buffer, size);
  lock_release(&fs_lock);

}

/*
Whenever a user process terminates, because it called exit or for any other reason, print the process's name and exit code, formatted as if printed by printf ("%s: exit(%d)\n", ...);. The name printed should be the full name passed to process_execute(), omitting command-line arguments. Do not print these messages when a kernel thread that is not a user process terminates, or when the halt system call is invoked. The message is optional when a process fails to load.
*/

// static void exit(int status){
//   ("%s: exit(%d)\n", ...);
// }