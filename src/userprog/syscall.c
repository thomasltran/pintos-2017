#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/cpu.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

#define NAME_MAX 14  // Maximum filename length per spec

/* Function declarations */
static void syscall_handler (struct intr_frame *);
static void write(int fd, const void * buffer, unsigned size);
static void exit(int status);
static bool is_valid_user_ptr(const void *ptr);

/* Initialize the system call handler */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* - - - - - - - - - - User Memory Validity Functions - - - - - - - - - - */

/* Validation Hierarchy:
  
    - Key idea is that these functions should cover every case of memory validation checks for `system calls`
    - Each validation function serves a specific purpose. From basic pointer validation to complete
      buffer checking. Choose the appropriate validation based on the system call's requirements
      and parameter types.

    1. is_valid_user_ptr -> checks if ptr is valid user pointer
    2. validate_user_buffer -> checks if buffer is valid user buffer
    3. get_user_byte -> checks if byte is valid user byte
    4. copy_user_string -> checks if string is valid user string

  Examples:

    Pattern 1: String Parameters (SYS_CREATE)
    1. validate_user_buffer(name, 1)  (Check pointer validity)
    2. copy_user_string()            (Create kernel copy)
    3. Check filename[0] != '\0'     (Explicit empty check)

    Pattern 2: Buffer Parameters (SYS_WRITE)
    1. validate_user_buffer(buffer, size)  (Full buffer check)
    2. Direct access after validation      
      - Safe because:
        a) Buffer remains mapped (Project 2 assumption)
        b) Locking prevents concurrent modification

    Pattern 3: Simple Values (SYS_EXIT)
    1. is_valid_user_ptr(status_ptr)  (Single pointer check)
    2. Direct read via *(int *)status_ptr

*/

/* Validates that a user pointer is valid by checking that:
   - It is not NULL
   - Points to user virtual address space 
   - Has a valid page mapping
   Parameters:
     - ptr: Pointer to validate
   Returns true if valid, false otherwise.
*/
static bool
is_valid_user_ptr(const void *ptr)
{
    struct thread *t = thread_current();
    return ptr != NULL 
        && is_user_vaddr(ptr)
        && pagedir_get_page(t->pagedir, ptr) != NULL;
}

/* Validate a buffer in user memory
   Parameters:
     - uaddr: User virtual address to validate
     - size: Size of buffer to validate
   Returns true if valid, false otherwise.
*/
bool validate_user_buffer(const void *uaddr, size_t size)
{
    /* if size is 0, return true */
    if (size == 0) 
      return true;

    if (uaddr == NULL || !is_user_vaddr(uaddr))
      return false;

    /* Check start and end addresses */
    const void *start = uaddr;
    const void *end = uaddr + size - 1;
    
    if (start >= PHYS_BASE || end >= PHYS_BASE)
      return false;

    /* Check page directory entries */
    struct thread *t = thread_current();
    uint32_t *pd = t->pagedir;
    
    /* Handle single page case */
    if (pg_no(start) == pg_no(end))
      // check if page is mapped; if not, return false
      return pagedir_get_page(pd, start) != NULL;

    /* Check each page in multi page buffer */
    for (void *page = pg_round_down(start); page <= end; page += PGSIZE) 
    {
        // check if page is mapped; if not, return false
        if (pagedir_get_page(pd, page) == NULL)
          return false;
    }
    
    return true;
}

/* Copies a byte from user memory into kernel memory.
   Parameters:
     - uaddr: User virtual address to copy from
     - kaddr: Kernel address to copy to
   Returns true if successful, false if invalid user pointer 
*/
static bool
get_user_byte(const uint8_t *uaddr, uint8_t *kaddr)
{
    if (!is_valid_user_ptr(uaddr))
        return false;
    
    *kaddr = *uaddr;
    return true;
}

/* Copy a string from user memory (similar to strlcpy)
   Parameters:
     - usrc: User source string
     - kdst: Kernel destination buffer
     - max_len: Maximum length to copy
   Returns true if successful, false if invalid user pointer or buffer overflow
*/
static bool
copy_user_string(const char *usrc, char *kdst, size_t max_len)
{
    /* Handle minimum buffer size */
    if (max_len == 0)
      return true;

    /* Loop string until max len or null terminator */
    for (size_t i = 0; i < max_len; i++) {
      uint8_t byte;
      int result = get_user_byte(usrc + i, &byte);

      if (!result){
        return false;
      }

      kdst[i] = byte;
      if (byte == '\0'){
        return true;
      }
    }
    /* Null terminate if we hit max length */
    kdst[max_len-1] = '\0';
    return true;
}

/* - - - - - - - - - - System Call Handler - - - - - - - - - - */

/* System call handler 
Parameters:
  - f: The interrupt frame for the system call
*/
static void
syscall_handler(struct intr_frame *f)
{
  //printf ("system call!\n");
  // Check if user pointer is valid (i.e. is in user space)
  if(f->esp >= PHYS_BASE || !is_valid_user_ptr(f->esp)){
    printf("exceed valid\n");
    exit(-1);
    return;
  }
  // Get the syscall number
  uint32_t sc_num = *((uint32_t *)(f->esp));

  // Syscalls Handled Via Switch Cases

  switch (sc_num) {

    // write
    case SYS_WRITE: {
      int fd = *((int *)(f->esp + 4));
      const void *buffer = *((void **)(f->esp + 8));
      unsigned size = *((unsigned *)(f->esp + 12));
      
      /* Validation using buffer range check */
      if (!validate_user_buffer(buffer, size)) {
        exit(-1);
        return;
      }
      write(fd, buffer, size);
      break;
    }
  
    // create
    case SYS_CREATE: {
      const char *name = *(const char **)(f->esp + 4);
      unsigned initial_size = *(unsigned *)(f->esp + 8);
      
      /* Validate filename pointer and buffer */
      char filename[NAME_MAX + 1];
      bool valid = validate_user_buffer(name, NAME_MAX + 1) && 
                   copy_user_string(name, filename, sizeof(filename));
      
      /* Explicit length check for maximum filename size */
      if (!valid || strlen(filename) >= NAME_MAX) {
        f->eax = 0;
      } else {
        lock_acquire(&fs_lock);
        f->eax = filesys_create(filename, initial_size) ? 1 : 0;
        lock_release(&fs_lock);
      }
      break;
    }

    // exit
    case SYS_EXIT: {
      int status = *((int *)(f->esp + 4));
      f->eax = status;
      exit(status);
      break;
    }
  }
}

/* - - - - - - - - - - Helper Functions for System Calls - - - - - - - - - - */

/* Write system call
Parameters:
  - fd: File descriptor to write to
  - buffer: Buffer containing data to write
  - size: Number of bytes to write
Returns:
  Number of bytes actually written, which may be less than size 
  if some bytes could not be written
*/
static void write(int fd, const void * buffer, unsigned size){
  lock_acquire(&fs_lock);
  putbuf(buffer, size);
  lock_release(&fs_lock);
  // TODO: how to write to a fd
}

/* Exit system call
   Prints process name and exit code when user process terminates.
   Format: "%s: exit(%d)\n" where %s is full program name without args
   Note: Only prints for user processes, not kernel threads or halt
   Optional message when process fails to load
*/
static void exit(int status){
  struct thread *thread_curr = thread_current(); // what if thread gets preempted here? best way to get the prog name—use stack?? 
  struct process * ps = thread_curr->ps;
  // not sure if we need the locking/disable intr
  
  printf("%s: exit(%d)\n", thread_curr->ps->user_prog_name, status);
  lock_acquire(&ps->ps_lock);
  ps->exit_status = status;
  free(thread_curr->ps->user_prog_name);
  lock_release(&ps->ps_lock);
  thread_exit();
}