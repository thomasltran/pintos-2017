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
#include "vm/page.h"
#include "vm/mappedfile.h"
#include "userprog/exception.h"
#include "vm/swap.h"

/* Function declarations */
static void syscall_handler (struct intr_frame *);
static int write(int fd, const void * buffer, unsigned size);
static bool is_valid_user_ptr(const void *ptr);
static bool validate_user_buffer(const void *uaddr, size_t size);
static bool get_user_byte(const uint8_t *uaddr, uint8_t *kaddr);
static bool copy_user_string(const char *usrc, char *kdst, size_t max_len);
static const char *buffer_check(struct intr_frame *f, int set_eax_err);
#ifdef VM
bool get_pinned_frames(void *uaddr, bool write, size_t size);
void unpin_frames(void *uaddr, size_t size);
#endif

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
    
    #ifdef VM
    struct supp_pt *supp_pt = t->supp_pt;

    lock_acquire(&vm_lock);
    bool ret = ptr != NULL && is_user_vaddr(ptr) && (pagedir_get_page(t->pagedir, ptr) != NULL || find_page(supp_pt, (void *)ptr) != NULL);
    lock_release(&vm_lock);

    #else
    bool ret = ptr != NULL && is_user_vaddr(ptr) && (pagedir_get_page(t->pagedir, ptr) != NULL);
    #endif

    return ret;
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

    /* Check start and end addresses */
    const void *start = uaddr;
    const void *end = uaddr + size - 1;
    
    if (start >= PHYS_BASE || end >= PHYS_BASE)
      return false;

    /* Check page directory entries */
    struct thread *t = thread_current();
    #ifdef VM
    struct supp_pt *supp_pt = t->supp_pt;
    #endif
    uint32_t *pd = t->pagedir;
    
    /* Handle single page case */
    if (pg_no(start) == pg_no(end))
      // check if page is mapped; if not, return false
      return pagedir_get_page(pd, start) != NULL;

    /* Check each page in multi page buffer */
    for (void *page = pg_round_down(start); page <= end; page += PGSIZE) 
    {
        // check if page is mapped; if not, return false
#ifdef VM

    // VOLATILE THANK YOU DR. BACK
    volatile char touch UNUSED;
    touch = *(volatile char *)page;
    //*(char *)page = touch;

    lock_acquire(&vm_lock);
    bool ret = pagedir_get_page(pd, page) == NULL && find_page(supp_pt, page) == NULL;
    lock_release(&vm_lock);

#else
    bool ret = pagedir_get_page(pd, page) == NULL;
#endif
    if (ret)
    {
      return false;
    }
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
    // if (pg_round_down(udst_ptr + i) != pg_round_down(udst_ptr + i - 1))
    // {
    //   printf("cross at %p\n", udst_ptr + i);
    // }

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
  char * filename = malloc(128); // Kernel buffer
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

/* - - - - - - - - - - System Call Handler - - - - - - - - - - */

/* System call handler 
Parameters:
  - f: The interrupt frame for the system call
*/
static void
syscall_handler(struct intr_frame *f)
{
  thread_current()->esp = f->esp;
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

  // Syscalls Handled Via Switch Cases
  switch (sc_num) {

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
        thread_current()->esp = NULL;
        f->eax = 0;
        break;
      }

#ifdef VM
      lock_acquire(&vm_lock);
      if (!get_pinned_frames((void *)buffer, false, size))
      {
        lock_release(&vm_lock);
        f->eax = -1;
        exit(-1);
      }
      lock_release(&vm_lock);
#else
      /* Validate buffer */
      if (!validate_user_buffer(buffer, size))
      {
        thread_current()->esp = NULL;
        f->eax = -1;
        exit(-1);
      }
#endif

      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      /* Validate FD */
      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      /* Perform read operation */
      struct file *file = cur->fd_table[fd];

      /* Allocate kernel buffer and read from file */
      uint8_t *kern_buf = malloc(size);
      if (kern_buf == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      bytes_read = file_read(file, kern_buf, size); // pinning or chunking read
      lock_release(&fs_lock);

      /* Copy to user buffer if read succeeded */
      if (bytes_read > 0) {
        /* Re-validate buffer since page status might have changed */
        if (!validate_user_buffer(buffer, bytes_read) ||
            !memcpy_to_user((void *)buffer, kern_buf, bytes_read))
        {
          f->eax = -1;
          exit(-1);
        }
      }
      
      free(kern_buf);
      f->eax = bytes_read;

      thread_current()->esp = NULL;

#ifdef VM
      lock_acquire(&vm_lock);
      unpin_frames((void *)buffer, size);
      lock_release(&vm_lock);
#endif

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

#ifdef VM
      lock_acquire(&vm_lock);
      if (!get_pinned_frames((void *)buffer, false, size))
      {
        lock_release(&vm_lock);
        f->eax = -1;
        exit(-1);
      }
      lock_release(&vm_lock);
#else
      /* Validate buffer */
      if (!validate_user_buffer(buffer, size))
      {
        thread_current()->esp = NULL;
        f->eax = -1;
        exit(-1);
      }
#endif

      off_t bytes = write(fd, buffer, size);
      f->eax = bytes;

      thread_current()->esp = NULL;
#ifdef VM
      lock_acquire(&vm_lock);
      unpin_frames((void *)buffer, size);
      lock_release(&vm_lock);
#endif
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

      struct thread *cur = thread_current();
      lock_acquire(&fs_lock);

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      {
        lock_release(&fs_lock);
        exit(-1);
      }
      struct file *cur_file = cur->fd_table[fd];
      file_seek(cur_file, position);

      lock_release(&fs_lock);

      thread_current()->esp = NULL;
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
      free((char *)filename);

      thread_current()->esp = NULL;
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
      free((char *)filename);

      /* Check if file exists */
      if (file == NULL) {
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      struct thread *cur = thread_current();

      if (cur->fd_table == NULL) {
        // calloc (since we know the size)
        cur->fd_table = calloc(FD_MAX, sizeof(struct file *));
        if (cur->fd_table == NULL)
        {
          f->eax = -1;
          lock_release(&fs_lock);
          exit(-1);
        }
      }

      /* Find a free fd */
      int fd = -1;
      for (int i = FD_MIN; i < FD_MAX; i++) {
        if (cur->fd_table[i] == NULL) {
          fd = i;
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

      //printf("tid %d open\n", cur->tid);

      thread_current()->esp = NULL;
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
      const char *filename = buffer_check(f, 0);

      lock_acquire(&fs_lock);
      f->eax = filesys_remove(filename) ? 1 : 0;
      lock_release(&fs_lock);
      free((char *)filename);

      thread_current()->esp = NULL;
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
      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      /* Validate FD */
      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      /* Get file size */
      f->eax = file_length(cur->fd_table[fd]);
      lock_release(&fs_lock);

      thread_current()->esp = NULL;
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

      thread_current()->esp = NULL;
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

      thread_current()->esp = NULL;
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
      free((char *)cmd_line);

      thread_current()->esp = NULL;
      break;

    // close
    case SYS_CLOSE: {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }

      int fd = *((int *)(f->esp + 4));
      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }
      file_close(cur->fd_table[fd]);
      cur->fd_table[fd] = NULL;

      lock_release(&fs_lock);

      thread_current()->esp = NULL;
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
      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }
      f->eax = file_tell(cur->fd_table[fd]);

      lock_release(&fs_lock);

      thread_current()->esp = NULL;
      break;
    }
    // halt
    case SYS_HALT:
      thread_current()->esp = NULL;
      shutdown_power_off();
      break;

    // mmap
#ifdef VM
    case SYS_MMAP:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4) || !is_valid_user_ptr(f->esp + 8))
      {
        f->eax = -1;
        exit(-1);
      }

      int fd = *((int *)(f->esp + 4));
      void *buffer = *((void **)(f->esp + 8));

      // check NULL and aligned
      if (pg_ofs(buffer) != 0 || buffer == 0)
      {
        f->eax = -1;
        thread_current()->esp = NULL;
        break;
      }

      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }
      struct file *file = cur->fd_table[fd];

      struct file *reopen = file_reopen(file); // operate on reopen vers
      if (reopen == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      off_t length = file_length(reopen);
      if (length == 0)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }
      lock_release(&fs_lock);

      int pages = (length + PGSIZE - 1) / PGSIZE; // round up formula
      void *curr = buffer;

      lock_acquire(&vm_lock);
      struct supp_pt *supp_pt = cur->supp_pt;

      for (int i = 0; i < pages; i++)
      {
        if (find_page(supp_pt, curr) != NULL) // alr mapped/overlap
        {
          f->eax = -1;
          thread_current()->esp = NULL;
          lock_release(&vm_lock);
          return;
        }
        curr += PGSIZE;
      }

      struct mapped_file *mapped_file = create_mapped_file(reopen, buffer, length);
      if (mapped_file == NULL)
      {
        f->eax = -1;
        lock_release(&vm_lock);
        exit(-1);
      }
      list_push_back(&cur->mapped_file_table->list, &mapped_file->elem);

      off_t ofs = 0;

      curr = buffer;
      for (int i = 0; i < pages; i++)
      {
        size_t page_read_bytes = PGSIZE;
        size_t page_zero_bytes = 0;

        if (length % PGSIZE != 0 && i == pages - 1) // zero out the remainder if read_bytes isn't PGSIZE
        {
          page_read_bytes = length % PGSIZE; // remainder
          page_zero_bytes = PGSIZE - page_read_bytes;
          struct page *page = create_page(curr, reopen, ofs, page_read_bytes, page_zero_bytes, true, MMAP, PAGED_OUT);
          if (page == NULL)
          {
            f->eax = -1;
            lock_release(&vm_lock);
            exit(-1);
          }

          page->map_id = mapped_file->map_id;

          struct hash_elem *ret = hash_insert(&supp_pt->hash_map, &page->hash_elem);
          ASSERT(ret == NULL);
          continue;
        }

        struct page *page = create_page(curr, reopen, ofs, page_read_bytes, page_zero_bytes, true, MMAP, PAGED_OUT);
        if (page == NULL)
        {
          f->eax = -1;
          lock_release(&vm_lock);
          exit(-1);
        }
        page->map_id = mapped_file->map_id;

        struct hash_elem *ret = hash_insert(&supp_pt->hash_map, &page->hash_elem);
        ASSERT(ret == NULL);

        curr += PGSIZE;
        ofs += page_read_bytes; // would be PGSIZE, remainder doesn't matter
      }

      f->eax = mapped_file->map_id;

      lock_release(&vm_lock);

      thread_current()->esp = NULL;
      break;
    }

    // munmap
    case SYS_MUNMAP:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }

      mapid_t mapping = *((int *)(f->esp + 4));
      struct thread *cur = thread_current();

      lock_acquire(&vm_lock);

      struct mapped_file *mapped_file = find_mapped_file(cur->mapped_file_table, mapping);

      if (mapped_file == NULL)
      {
        f->eax = -1;
        lock_release(&vm_lock);
        break;
      }

      if (!get_pinned_frames(mapped_file->addr, true, mapped_file->length))
      {
        f->eax = -1;
        lock_release(&vm_lock);
        break;
      }

      int pages = (mapped_file->length + PGSIZE - 1) / PGSIZE; // round up formula
      void *curr = mapped_file->addr;

      struct supp_pt *supp_pt = cur->supp_pt;

      for (int i = 0; i < pages; i++)
      {
        struct page *page = find_page(supp_pt, curr); // continguous
        ASSERT(page != NULL);
        ASSERT(page->page_location == PAGED_IN);
        ASSERT(page->page_status == MMAP);

        struct frame *frame = get_page_frame(page);
        ASSERT(frame != NULL);

        if (!pagedir_is_dirty(cur->pagedir, page->uaddr))
        {
          page_frame_freed(frame); // will unpin

          page->page_status = MUNMAP; // used in page fault

          curr += PGSIZE;
          continue;
        }

        lock_release(&vm_lock);
        lock_acquire(&fs_lock);

        off_t wrote = file_write_at(mapped_file->file, page->uaddr, page->read_bytes, page->ofs);
        ASSERT((uint32_t)wrote == page->read_bytes);

        lock_release(&fs_lock);
        lock_acquire(&vm_lock);

        curr += PGSIZE;

        page_frame_freed(frame); // will unpin the frames

        page->page_status = MUNMAP;
      }

      list_remove(&mapped_file->elem);
      free(mapped_file);

      lock_release(&vm_lock);

      thread_current()->esp = NULL;

      break;
    }
#endif
    default:
      thread_current()->esp = NULL;
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
    struct thread *cur = thread_current();
    lock_acquire(&fs_lock);
    if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
    {
      lock_release(&fs_lock);
      exit(-1);
    }
    struct file *cur_file = cur->fd_table[fd];

    unsigned count = 0;
    while(count < size){
      int buffer_size = (size-count) > 207 ? 207 : size-count;

      off_t bytes = file_write(cur_file, buffer+count, buffer_size);
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
  struct process *ps = thread_curr->ps;
  ASSERT(ps != NULL);

  printf("%s: exit(%d)\n", thread_curr->ps->user_prog_name, status);

  lock_acquire(&ps->ps_lock);
  ps->exit_status = status;
  lock_release(&ps->ps_lock);

  thread_current()->esp = NULL;

  thread_exit();
}

#ifdef VM
void unpin_frames(void *uaddr, size_t size)
{
  const void *start = uaddr;
  const void *end = uaddr + size - 1;
  struct thread *cur = thread_current();

  ASSERT(start < PHYS_BASE || end < PHYS_BASE);

  int pages = (pg_no(end) - pg_no(start)) + 1; // num of pages the buff spans across, didntt need in mapped files bc its aligned

  void *curr = uaddr;

  struct supp_pt *supp_pt = cur->supp_pt;

  for (int i = 0; i < pages; i++)
  {
    struct page *page = find_page(supp_pt, curr);
    ASSERT(page != NULL && page->page_location == PAGED_IN)

    struct frame *frame = get_page_frame(page);
    ASSERT(frame != NULL);
    ASSERT(frame->pinned == true);

    frame->pinned = false;
    // printf("page unpin %p\n", page->uaddr);
    curr += PGSIZE;
  }
}

bool get_pinned_frames(void *uaddr, bool write, size_t size)
{
  const void *start = uaddr;
  const void *end = uaddr + size - 1;
  struct thread *cur = thread_current();

  if (start >= PHYS_BASE || end >= PHYS_BASE)
    return false;

  int pages = (pg_no(end) - pg_no(start)) + 1; // num of pages the buff spans across, didntt need in mapped files bc its aligned

  void *curr = uaddr;

  struct supp_pt *supp_pt = cur->supp_pt;

  for (int i = 0; i < pages; i++)
  {
    struct page *page = find_page(supp_pt, curr); // continguous

    if (page != NULL && page->page_location == PAGED_IN) // alr mapped/overlap
    {
      struct frame *frame = get_page_frame(page);
      ASSERT(frame != NULL);

      frame->pinned = true;
      curr += PGSIZE;
      continue;
    }

    void *esp = NULL;
    struct thread *thread_cur = thread_current();

    esp = thread_cur->esp;

    bool stack_growth = false;

    if (curr >= esp - 32 && curr > PHYS_BASE - STACK_LIMIT)
      stack_growth = true;

    struct page *fault_page = NULL;

    if (stack_growth)
    {
      fault_page = create_page(curr, NULL, 0, 0, PGSIZE, true, STACK, PAGED_OUT);
      if (fault_page == NULL)
      {
        return false;
      }

       ASSERT(thread_current()->supp_pt != NULL)
       struct hash_elem *ret = hash_insert(&thread_current()->supp_pt->hash_map, &fault_page->hash_elem);
       ASSERT(ret == NULL);
    }
    else
    {
      fault_page = find_page(thread_cur->supp_pt, curr);
    }

    if (fault_page == NULL)
    {
      return false;
    }

    if (fault_page->page_status == MUNMAP)
    {
      return false;
    }

    if (write && !fault_page->writable)
    {
      return false;
    }

    void *upage = pg_round_down(curr);

    ASSERT(pagedir_get_page(thread_cur->pagedir, upage) == NULL);

    struct frame *frame = ft_get_page_frame(thread_current(), fault_page, true);
    uint8_t *kpage = frame->kaddr;

    ASSERT(kpage != NULL);

    ASSERT(pg_round_down(curr) == pg_round_down(fault_page->uaddr));

    bool in_swap = fault_page->page_location == SWAP;
    fault_page->page_location = PAGED_IN;

    if (pagedir_get_page(thread_cur->pagedir, upage) != NULL || pagedir_set_page(thread_cur->pagedir, upage, kpage, fault_page->writable) == false)
    {
      page_frame_freed(frame);
      return false;
    }

    if (!stack_growth)
    {
      if ((fault_page->page_status == DATA_BSS || fault_page->page_status == STACK) && in_swap)
      {
        ASSERT(fault_page->swap_index != UINT32_MAX);

        st_read_at(kpage, fault_page->swap_index);
        fault_page->swap_index = UINT32_MAX;
      }
      else
      {
        lock_release(&vm_lock);
        lock_acquire(&fs_lock);

        file_seek(fault_page->file, fault_page->ofs);
        if (file_read(fault_page->file, kpage, fault_page->read_bytes) != (int)fault_page->read_bytes)
        {
          lock_release(&fs_lock);
          return false;
        }

        lock_release(&fs_lock);
        lock_acquire(&vm_lock);
      }
    }

    if(!in_swap){
      memset(kpage + fault_page->read_bytes, 0, fault_page->zero_bytes);
    }

    ASSERT(fault_page->page_location == PAGED_IN);
    ASSERT(frame->pinned == true);
    ASSERT(frame->thread == cur);

    curr += PGSIZE;
  }
  return true;
}
#endif