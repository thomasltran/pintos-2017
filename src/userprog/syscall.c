#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/gdt.h"
#include "threads/flags.h"
#include "threads/palloc.h"
#include "threads/tss.h"
#include "lib/user/pthread-def.h"

void start_thread(void *aux);

/* Function declarations */
static void syscall_handler (struct intr_frame *);
static int write(int fd, const void * buffer, unsigned size);
static bool is_valid_user_ptr(const void *ptr);
static bool validate_user_buffer(const void *uaddr, size_t size);
static bool get_user_byte(const uint8_t *uaddr, uint8_t *kaddr);
static bool copy_user_string(const char *usrc, char *kdst, size_t max_len);
static const char *buffer_check(struct intr_frame *f, int set_eax_err);
static bool check_fd(int fd);
static int reserve_pthread_mutex_slot(void);


struct pthread_mutex_info
{
  struct lock kernel_lock;
  bool inuse;
};

static struct pthread_mutex_info * pthread_mutex_table = NULL;

static int reserve_pthread_mutex_slot()
{
  for (int i = 0; i < MUTEX_COUNT; i++)
  {
    struct pthread_mutex_info * pthread_mutex_info = &pthread_mutex_table[i];
    if (pthread_mutex_info->inuse == false)
    {
      pthread_mutex_info->inuse = true;
      return i;
    }
  }
  return -1;
}

/* Initialize the system call handler */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

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
        && pagedir_get_page(t->pcb->pagedir, ptr) != NULL;
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
    uint32_t *pd = t->pcb->pagedir;
    
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
      // printf("count %d\n", i);
      uint8_t byte;
      const uint8_t * src = (const uint8_t *)usrc + i;
      int result = get_user_byte(src, &byte);

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

// checks the program name/file name, returns it if good; filename at esp + 4
// pass in the value of an error (ex: 0 for false, -1, etc.) in the int
static const char *buffer_check(struct intr_frame *f, int set_eax_err)
{
  char *filename = malloc(128); // Kernel buffer, set size for now
  if (filename == NULL)
  {
    f->eax = set_eax_err;
    exit(-1);
  }

  const char *cmd_line = *((char **)(f->esp + 4));

  bool valid = validate_user_buffer(f->esp + 4, 128);
  if (!valid)
  {
    free(filename);
    f->eax = set_eax_err;
    exit(-1);
  }
  valid = copy_user_string(cmd_line, filename, 128);

  /* Check if filename is valid */
  if (!valid)
  {
    free(filename);
    f->eax = set_eax_err;
    exit(-1);
  }
  return filename;
}

static bool check_fd(int fd)
{
  return !(fd < FD_MIN || fd >= FD_MAX || thread_current()->pcb->fd_table == NULL || thread_current()->pcb->fd_table[fd] == NULL);
}

static bool
install_page(void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL && pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

void start_thread(void *aux)
{
  struct thread * cur = thread_current();
  struct pthread_args * args = (struct pthread_args *)aux;
  ASSERT(args != NULL);

  cur->pthread_args = args;
  cur->pcb = args->pcb;

  // from process_activate
  // tell to use this pagedir for this new pthread
  pagedir_activate(cur->pcb->pagedir);
  tss_update();

  // from start_process
  struct intr_frame if_;
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  if_.eip = (void (*) (void))args->wrapper;
  if_.esp = args->esp;

  // free(args);
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
}

static void
syscall_handler(struct intr_frame *f)
{
  // Check if user pointer is valid (i.e. is in user space)
  if (f->esp >= PHYS_BASE || !is_valid_user_ptr(f->esp))
  {
    exit(-1);
  }

  for (int i = 0; i < 4; i++)
  {
    uint8_t *temp = (uint8_t *)(f->esp + i); // check byte by byte for sc-boundary-3
    if (!is_valid_user_ptr(temp + i))
    {
      exit(-1);
    }
  }

  // Get the syscall number
  uint32_t sc_num = *((uint32_t *)(f->esp));
  struct thread * cur = thread_current();

  switch (sc_num) {

  case SYS_PTHREAD_CREATE:
  {
    void *wrapper_addr = *(void **)(f->esp + 4);
    void *routine_addr = *(void **)(f->esp + 8);
    void *arg = *(void **)(f->esp + 12);

    void (*wrapper)(void *, void *) = (void (*)(void *, void *))wrapper_addr;
    void *(*start_routine)(void *) = (void *(*)(void *))routine_addr;

    if (cur->pcb->bitmap == NULL)
    {
      cur->pcb->bitmap = bitmap_create(33);
      cur->pcb->multithread = true;
      bitmap_mark(cur->pcb->bitmap, 0); // mark main thread as used
    }

    lock_acquire(&cur->pcb->lock);
    size_t bitmap_index = bitmap_scan_and_flip(cur->pcb->bitmap, 1, 1, false);
    lock_release(&cur->pcb->lock);
    if (bitmap_index == BITMAP_ERROR)
    {
      printf("failed create\n");
      f->eax = -1;
      break;
    }

    void *stack_top = PHYS_BASE - (bitmap_index * PTHREAD_SIZE); // chunk
    //printf("create st %p\n", stack_top);
    // printf("create %d\n", bitmap_index);
    // same from setup_stack stuff
    // do we want to exit if oom
    uint8_t *kpage;
    bool success = false;
    kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if (kpage == NULL)
    {
      printf("failed create\n");

      bitmap_reset(cur->pcb->bitmap, bitmap_index);
      f->eax = -1;
      break;
    }
    success = install_page(stack_top - PGSIZE, kpage, true);
    if (!success)
    {
      printf("failed create\n");

      palloc_free_page(kpage);
      bitmap_reset(cur->pcb->bitmap, bitmap_index);
      f->eax = -1;
      break;
    }
    
    // arg passing rev order
    stack_top -= sizeof(void *);
    *(void **)stack_top = arg;

    stack_top -= sizeof(void *);
    *(void **)stack_top = start_routine;

    stack_top -= sizeof(void *);
    *(void **)stack_top = 0;

    struct pthread_args *pthread_args = malloc(sizeof(struct pthread_args));
    pthread_args->wrapper = wrapper;
    pthread_args->esp = stack_top;
    pthread_args->bitmap_index = bitmap_index;
    pthread_args->pcb = cur->pcb;
    pthread_args->kpage = kpage;
    pthread_args->res = NULL;
    sema_init(&pthread_args->pthread_exit, 0);

    tid_t tid = thread_create("pthread", NICE_DEFAULT, start_thread, pthread_args);
    if (tid == TID_ERROR)
    {
      printf("failed create\n");

      pagedir_clear_page(cur->pcb->pagedir, stack_top - PGSIZE);
      palloc_free_page(kpage);
      bitmap_reset(cur->pcb->bitmap, bitmap_index);
      free(pthread_args);
      f->eax = -1;
      break;
    }
    // we can do this after, only the main thread needs to know
    pthread_args->pthread_tid = tid;

    list_push_front(&cur->pcb->list, &pthread_args->elem);
    f->eax = tid;
    break;
  }

  case SYS_PTHREAD_EXIT:
  {
    void *res = *(void **)(f->esp + 4);

    // todo: add support for pthread exit main thread
    lock_acquire(&cur->pcb->lock);
    ASSERT(bitmap_test(cur->pcb->bitmap, cur->pthread_args->bitmap_index) == true);
    lock_release(&cur->pcb->lock);

    cur->pthread_args->res = res;
    exit(0);
    break;
  }

  case SYS_PTHREAD_JOIN:
  {
    tid_t pthread_tid = *(tid_t *)(f->esp + 4);
    void **res = *(void ***)(f->esp + 8);

    bool found = false;
    struct pthread_args *pthread_args = NULL;

    for (struct list_elem *e = list_begin(&cur->pcb->list); e != list_end(&cur->pcb->list); e = list_next(e))
    {
      pthread_args = list_entry(e, struct pthread_args, elem);
      if (pthread_args->pthread_tid == pthread_tid)
      {
        found = true;
        list_remove(e);
        break;
      }
    }
    ASSERT(found == true);

    sema_down(&pthread_args->pthread_exit);
    if (res != NULL)
    {
      *res = pthread_args->res; // sketchy, but works?
    }

    free(pthread_args);
    f->eax = pthread_tid;
    break;
  }

  case SYS_MUTEX_INIT:
  {
    pthread_mutex_t * pthread_mutex = *(pthread_mutex_t **)(f->esp + 4);

    if (pthread_mutex_table == NULL)
    {
      pthread_mutex_table = calloc(MUTEX_COUNT, sizeof(struct pthread_mutex_info));
    }

    int pthread_mutex_id = reserve_pthread_mutex_slot();
    ASSERT(pthread_mutex_id != -1);

    struct pthread_mutex_info * pthread_mutex_info = &pthread_mutex_table[pthread_mutex_id];

    lock_init(&pthread_mutex_info->kernel_lock);
    pthread_mutex->pthread_mutex_id = pthread_mutex_id;
    f->eax = 0;
    break;
  }

  case SYS_MUTEX_LOCK:
  {
    pthread_mutex_t * pthread_mutex = *(pthread_mutex_t **)(f->esp + 4);

    struct pthread_mutex_info * pthread_mutex_info = &pthread_mutex_table[pthread_mutex->pthread_mutex_id];

    lock_acquire(&pthread_mutex_info->kernel_lock);
    f->eax = 0;
    break;
  }

  case SYS_MUTEX_UNLOCK:
  {
    pthread_mutex_t * pthread_mutex = *(pthread_mutex_t **)(f->esp + 4);

    struct pthread_mutex_info * pthread_mutex_info = &pthread_mutex_table[pthread_mutex->pthread_mutex_id];

    lock_release(&pthread_mutex_info->kernel_lock);
    f->eax = 0;
    break;
  }

  case SYS_MUTEX_DESTROY:
  {
    pthread_mutex_t * pthread_mutex = *(pthread_mutex_t **)(f->esp + 4);

    struct pthread_mutex_info * pthread_mutex_info = &pthread_mutex_table[pthread_mutex->pthread_mutex_id];

    pthread_mutex_info->inuse = false;
    f->eax = 0;
    break;
  }

    /* comments here that checks ptrs, buffers, fd, extract func params etc. mostly applies to the other syscall cases as well */
    // read
    case SYS_READ: {
      // check ptrs
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4) || !is_valid_user_ptr(f->esp + 8) || !is_valid_user_ptr(f->esp + 12))
      {
        f->eax = -1;
        exit(-1);
      }

      // extract func params
      int fd = *((int *)(f->esp + 4));
      const void *buffer = *((void **)(f->esp + 8));
      unsigned size = *((unsigned *)(f->esp + 12));
      int bytes_read = -1; // default return value (if error when reading)

      if (size == 0)
      {
        f->eax = 0;
        break;
      }

      /* Validate buffer */
      if (!validate_user_buffer(buffer, size))
      {
        f->eax = -1;
        exit(-1);
      }

      /* Validate FD */
      if (!check_fd(fd))
      {
        f->eax = -1;
        exit(-1);
      }

      lock_acquire(&fs_lock);

      /* Perform read operation */
      struct file *file = cur->pcb->fd_table[fd];

      /* Allocate kernel buffer and read from file */
      uint8_t *kern_buf = malloc(size);
      if (kern_buf == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      bytes_read = file_read(file, kern_buf, size);

      /* Copy to user buffer if read succeeded */
      if (bytes_read > 0) {
          /* Re-validate buffer since page status might have changed */
          if (!validate_user_buffer(buffer, bytes_read) || 
              !memcpy_to_user((void*)buffer, kern_buf, bytes_read)) {
            f->eax = -1;
            lock_release(&fs_lock);
            exit(-1);
          }
      }
      
      free(kern_buf);
      lock_release(&fs_lock);
      f->eax = bytes_read;
      break;
    }

    // write
    case SYS_WRITE: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4) || !is_valid_user_ptr(f->esp + 8) || !is_valid_user_ptr(f->esp + 12))
      {
        f->eax = -1;
        exit(-1);
      }

      int fd = *((int *)(f->esp + 4));
      const void *buffer = *((void **)(f->esp + 8));
      unsigned size = *((unsigned *)(f->esp + 12));

      /* Validation using buffer range check */
      if (!validate_user_buffer(buffer, size)) {
        f->eax = -1;
        exit(-1);
      }
      off_t bytes = write(fd, buffer, size);
      f->eax = bytes;
      break;
    }

    case SYS_SEEK:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4) || !is_valid_user_ptr(f->esp + 8))
      {
        exit(-1);
      }

      int fd = *(int *)(f->esp + 4);
      unsigned position = *(unsigned *)(f->esp + 8);

      if (!check_fd(fd))
      {
        f->eax = -1;
        exit(-1);
      }

      lock_acquire(&fs_lock);

      struct file *cur_file = cur->pcb->fd_table[fd];
      file_seek(cur_file, position);

      lock_release(&fs_lock);
      break;
    }

    // create
    case SYS_CREATE: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4) || !is_valid_user_ptr(f->esp + 8))
      {
        f->eax = 0;
        exit(0);
      }

      const char *filename = buffer_check(f, 0); // called in other syscalls with a filename buffer too, but checks byte by byte for validity
      unsigned initial_size = *(unsigned *)(f->esp + 8);

      if (strnlen(filename, NAME_MAX) >= NAME_MAX)
      {
        f->eax = 0;
      }
      else
      {
        lock_acquire(&fs_lock);
        f->eax = filesys_create(filename, initial_size) ? 1 : 0;
        lock_release(&fs_lock);
      }
      free((char*)filename);
      break;
    }

    // open
    case SYS_OPEN:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }
      const char *filename = buffer_check(f, -1);

      /* Open file */
      lock_acquire(&fs_lock);
      struct file *file = filesys_open(filename);
      free((char*)filename);

      /* Check if file exists */
      if (file == NULL) {
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      lock_release(&fs_lock);

      if (cur->pcb->fd_table == NULL) {
        // calloc (since we know the size)
        cur->pcb->fd_table = calloc(FD_MAX, sizeof(struct file *));
        if (cur->pcb->fd_table == NULL)
        {
          f->eax = -1;
          exit(-1);
        }
      }

      /* Find a free fd */
      int fd = -1;
      for (int i = FD_MIN; i < FD_MAX; i++) {
        if (cur->pcb->fd_table[i] == NULL) {
          fd = i;
          break;
        }
      }

      lock_acquire(&fs_lock);

      /* Table is full (i.e. no free fds) */
      if (fd ==- 1) {
        file_close(file);
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      /* Store file pointer in FD table */
      cur->pcb->fd_table[fd] = file;
      file_seek(file, 0); // reset file position to 0 (start of file)
      lock_release(&fs_lock);
      f->eax = fd;
      break;
    }

    // remove
    case SYS_REMOVE:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = 0;
        exit(0);
      }
      const char *file = buffer_check(f, 0);

      lock_acquire(&fs_lock);
      f->eax = filesys_remove(file) ? 1 : 0;
      free((char*)file);
      lock_release(&fs_lock);

      break;
    }

    // file size
    case SYS_FILESIZE: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }

      int fd = *((int *)(f->esp + 4));

      /* Validate FD */
      if (!check_fd(fd))
      {
        f->eax = -1;
        exit(-1);
      }

      lock_acquire(&fs_lock);

      /* Get file size */
      f->eax = file_length(cur->pcb->fd_table[fd]);
      lock_release(&fs_lock);
      break;
    }

    // exit
    case SYS_EXIT: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }
      else
      {
        int status = *((int *)(f->esp + 4));
        f->eax = status;
        exit(status);
      }
      break;
    }

    // wait
    case SYS_WAIT:
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }
      else
      {
        tid_t pid = *((tid_t *)(f->esp + 4));
        f->eax = process_wait(pid);
      }

      break;

    // exec
    case SYS_EXEC:
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }
      const char *cmd_line = buffer_check(f, -1);
      f->eax = process_execute(cmd_line);
      free((char*)cmd_line);
      break;

    // close
    case SYS_CLOSE: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }

      int fd = *((int *)(f->esp + 4));

      if (!check_fd(fd))
      {
        f->eax = -1;
        exit(-1);
      }

      lock_acquire(&fs_lock);

      file_close(cur->pcb->fd_table[fd]);
      cur->pcb->fd_table[fd] = NULL;

      lock_release(&fs_lock);

      break;
    }

    // tell
    case SYS_TELL: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }

      int fd = *((int *)(f->esp + 4));

      if (!check_fd(fd))
      {
        f->eax = -1;
        exit(-1);
      }

      lock_acquire(&fs_lock);

      f->eax = file_tell(cur->pcb->fd_table[fd]);

      lock_release(&fs_lock);
      break;
    }
    // halt
    case SYS_HALT:
      shutdown_power_off();
      break;

    default:
      f->eax = -1;
      exit(-1);
      break;
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
static int write(int fd, const void * buffer, unsigned size){
  if(fd == 1){
    lock_acquire(&fs_lock);
    unsigned count = 0;
    void *pos = (void *)buffer;
    while(count < size){
      int buffer_size = (size-count) > 200 ? 200 : size-count;

      putbuf(pos, buffer_size);
      count += buffer_size;
      pos = (void *)buffer + count;
    }
    lock_release(&fs_lock);
    return count;
  }
  else
  {
    if (!check_fd(fd))
    {
      exit(-1);
    }

    lock_acquire(&fs_lock);
    struct file *cur_file = thread_current()->pcb->fd_table[fd];

    unsigned count = 0;
    while (count < size)
    {
      int buffer_size = (size - count) > 207 ? 207 : size - count;

      off_t bytes = file_write(cur_file, buffer + count, buffer_size);
      if (bytes <= 0)
      {
        break;
      }
      count += bytes;
    }
    lock_release(&fs_lock);
    return count;
  }
}

/* Exit system call
   Prints process name and exit code when user process terminates.
   Format: "%s: exit(%d)\n" where %s is full program name without args
   Note: Only prints for user processes, not kernel threads or halt
   Optional message when process fails to load
*/
void exit(int status){
  struct thread *thread_curr = thread_current();

  if (!thread_curr->pcb->multithread || (thread_curr->pcb->multithread && thread_curr->pthread_args == NULL))
  {
    struct parent_child *parent_child = thread_curr->parent_child;

    printf("%s: exit(%d)\n", thread_curr->parent_child->user_prog_name, status);

    lock_acquire(&parent_child->parent_child_lock);
    parent_child->exit_status = status;
    lock_release(&parent_child->parent_child_lock);
  }

  thread_exit();
}