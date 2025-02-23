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
#include "filesys/filesys.h"
#include <string.h> 

#define NAME_MAX 14  // Maximum filename length per spec
#define FD_MIN 3    // Reserve 0 (stdin), 1 (stdout), 2 (stderr)
#define FD_MAX 128 // maximum number of fds (per Dr Back reccomendation)

/* Function declarations */
static void syscall_handler (struct intr_frame *);
static void write(int fd, const void * buffer, unsigned size);
static void exit(int status);
static bool is_valid_user_ptr(const void *ptr);
static bool validate_user_buffer(const void *uaddr, size_t size);
static bool get_user_byte(const uint8_t *uaddr, uint8_t *kaddr);
static bool copy_user_string(const char *usrc, char *kdst, size_t max_len);

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

    1. is_valid_user_ptr    -> Basic pointer validity check 
    2. validate_user_buffer -> Full buffer accessibility verification
    3. get_user_byte        -> Safe single-byte read from user space
    4. copy_user_string     -> Secure string copying to kernel space
    5. put_user_byte        -> Single byte write to user space
    6. memcpy_to_user       -> Bulk data transfer to user buffers

  Examples:

    Pattern 1: String Parameters (SYS_CREATE)
    1. validate_user_buffer(name, 1)  (Check pointer validity)
    2. copy_user_string()             (Create kernel copy)
    3. Check filename[0] != '\0'      (Explicit empty check)

    Pattern 2: Buffer Parameters (SYS_WRITE)
    1. validate_user_buffer(buffer, size)  (Full buffer check)
    2. Direct access after validation      
    - Safe because:
      a) Buffer remains mapped (Project 2 assumption)
      b) Locking prevents concurrent modification

    Pattern 3: Simple Values (SYS_EXIT)
    1. is_valid_user_ptr(status_ptr)      (Single pointer check)
    2. Direct read via *(int *)status_ptr

    Pattern 4: Buffer Input (SYS_READ)
    1. validate_user_buffer(buffer, size) (Initial buffer check)
    2. Read into kernel buffer            (Isolate user memory)
    3. memcpy_to_user()                   (Safe bulk copy)
    4. Re-validate buffer                 (Post copy check)
    - Prevents:
      a) Page faults during file ops
      b) Stale pointer dereferences

    Implementation Notes:
    - Prefer memcpy_to_user() over put_user_byte() for bulk data
    - Always pair copy_user_string() with validate_user_buffer()
    - Re-validation is critical after long operations
    - Kernel buffers act as safe intermediaries during I/O
*/

/* Validates that a user pointer is valid by checking that:
   - It is not NULL
   - Points to user virtual address space (below PHYS_BASE)
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

/* Writes a byte from kernel memory to user memory.
   Parameters:
     - uaddr: User virtual address to write to
     - kbyte: Kernel byte to copy
   Returns true if successful, false if invalid user pointer
*/
static bool
put_user_byte(uint8_t *uaddr, uint8_t kbyte)
{
    /* Check if user pointer is valid */
    if (!is_valid_user_ptr(uaddr))
        return false;

    /* Write byte to user memory */
    *uaddr = kbyte;
    return true; // if byte copied successfully (no page faults)
}

/* Copies data from kernel memory to user memory.
   Parameters:
     - udst: User destination buffer
     - ksrc: Kernel source buffer
     - max_len: Maximum length to copy
   Returns true if successful, false if invalid user pointer or buffer overflow
*/
static bool
memcpy_to_user(void *udst, const void *ksrc, size_t max_len)
{
  /* user destination & kernel source pointers */
  uint8_t *udst_ptr = udst;
  const uint8_t *ksrc_ptr = ksrc;

  /* Copy each byte from kernel to user */
  for (size_t i = 0; i < max_len; i++) {
    if (!put_user_byte(udst_ptr + i, ksrc_ptr[i])) {
      return false; // if any byte fails to copy
    }
  }
  return true; // if all byte(s) copied successfully (no page faults)
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
    // read
    case SYS_READ: {
      int fd = *((int *)(f->esp + 4));
      const void *buffer = *((void **)(f->esp + 8));
      unsigned size = *((unsigned *)(f->esp + 12));
      int bytes_read = -1; // default return value (if error when reading)
      
      /* Validate FD */
      struct thread *cur = thread_current();
      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table[fd] == NULL) {
        f->eax = -1;
        break;
      }

      /* Validate buffer */
      if (!validate_user_buffer(buffer, size)) {
        f->eax = -1;
        break;
      }

      /* Perform read operation */
      lock_acquire(&fs_lock);
      struct file *file = cur->fd_table[fd];

      /* Allocate kernel buffer and read from file */
      uint8_t *kern_buf = malloc(size);
      bytes_read = file_read(file, kern_buf, size);
      
      /* Copy to user buffer if read succeeded */
      if (bytes_read > 0) {
          /* Re-validate buffer since page status might have changed */
          if (!validate_user_buffer(buffer, bytes_read) || 
              !memcpy_to_user(buffer, kern_buf, bytes_read)) {
              bytes_read = -1;
          }
      }
      
      free(kern_buf);
      lock_release(&fs_lock);
      f->eax = bytes_read;
      break;
    }

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
      char filename[NAME_MAX + 1];  // Kernel buffer

      /* Validate filename pointer and buffer */
      bool valid = validate_user_buffer(name, NAME_MAX + 1) && 
                   copy_user_string(name, filename, sizeof(filename));
      
      /* length check for maximum filename size */
      if (!valid || strlen(filename) >= NAME_MAX) {
        f->eax = 0;
      } else {
        lock_acquire(&fs_lock);
        f->eax = filesys_create(filename, initial_size) ? 1 : 0;
        lock_release(&fs_lock);
      }
      break;
    }

    // open
    case SYS_OPEN: { 
      const char *name = *(const char **)(f->esp + 4);
      char filename[NAME_MAX + 1];  // Kernel buffer

      /* Validate filename pointer and buffer */
      bool valid = validate_user_buffer(name, NAME_MAX + 1) && 
               copy_user_string(name, filename, sizeof(filename));

      /* Check if filename is valid */
      if (!valid){
        f->eax = -1;
        break;
      } 

      /* Open file */
      lock_acquire(&fs_lock);
      struct file *file = filesys_open(filename);

      /* Check if file exists */
      if (file == NULL) {
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      /* Allocate FD table if this is first open */
      struct thread *cur = thread_current(); 
      if (cur->fd_table == NULL) {
        // calloc (since we know the size)
        cur->fd_table = calloc(FD_MAX, sizeof(struct file *));
        cur->fd_count = FD_MIN;
      }

      /* Find a free fd */
      int fd = -1;
      for (int i = FD_MIN; i < FD_MAX; i++) {
        if (cur->fd_table[i] == NULL) {
          fd = i;
          cur->fd_count = (i + 1) % FD_MAX;
          break;
        }
      }

      /* Table is full (i.e. no free fds) */
      if (fd ==- 1) {
        file_close(file);
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      /* Store file pointer in FD table */
      cur->fd_table[fd] = file;
      file_seek(file, 0); // reset file position to 0 (start of file)
      lock_release(&fs_lock);
      f->eax = fd;
      break;
    }

    // file size
    case SYS_FILESIZE: {
      int fd = *((int *)(f->esp + 4));
      struct thread *cur = thread_current();
      
      /* Validate FD */
      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table[fd] == NULL) {
        f->eax = -1;
        break;
      }

      /* Get file size */
      lock_acquire(&fs_lock);
      f->eax = file_length(cur->fd_table[fd]);
      lock_release(&fs_lock);
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