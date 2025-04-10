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
#include "devices/block.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"

/* Function declarations */
static void syscall_handler (struct intr_frame *);
static int write(int fd, const void * buffer, unsigned size);
static bool is_valid_user_ptr(const void *ptr);
static bool validate_user_buffer(const void *uaddr, size_t size);
static bool get_user_byte(const uint8_t *uaddr, uint8_t *kaddr);
static bool copy_user_string(const char *usrc, char *kdst, size_t max_len);
static const char *buffer_check(struct intr_frame *f, int set_eax_err);

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
      // // printf("count %d\n", i);
      uint8_t byte;
      const uint8_t *src = (const uint8_t *)usrc + i;
      int result = get_user_byte(src, &byte);

      if (!result)
      {
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
   char *filename = malloc(PATH_MAX + 1); // Kernel buffer, set size for now
   if (filename == NULL)
   {
      f->eax = set_eax_err;
      exit(-1);
   }

  const char *cmd_line = *((char **)(f->esp + 4));

  bool valid = validate_user_buffer(f->esp + 4, PATH_MAX + 1);
  if (!valid)
  {
    free(filename);
    f->eax = set_eax_err;
    exit(-1);
  }
  valid = copy_user_string(cmd_line, filename, PATH_MAX + 1);

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
        f->eax = 0;
        break;
      }

      /* Validate buffer */
      if (!validate_user_buffer(buffer, size))
      {
        f->eax = -1;
        exit(-1);
      }

      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      /* Validate FD */
      struct file_desc * file_desc = &cur->fd_table[fd];

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || (file_desc->file == NULL && file_desc->dir == NULL))
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      /* Perform read operation */
      struct file *file = file_desc->file;

      /* Allocate kernel buffer and read from file */
      uint8_t *kern_buf = malloc(BLOCK_SECTOR_SIZE);
      if (kern_buf == NULL)
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      unsigned count = 0;
      while (count < size)
      {
        off_t bytes_left = BLOCK_SECTOR_SIZE < (size - count) ? BLOCK_SECTOR_SIZE : size - count;
        int temp_bytes_read = file_read(file, kern_buf, bytes_left);
        /* Copy to user buffer if read succeeded */
        if (temp_bytes_read > 0)
        {
          /* Re-validate buffer since page status might have changed */
          if (!validate_user_buffer(buffer + count, temp_bytes_read) ||
              !memcpy_to_user((void *)(buffer + count), kern_buf, temp_bytes_read))
          {
            lock_release(&fs_lock);
            f->eax = -1;
            exit(-1);
          }
        }
        count += temp_bytes_read;
      }
      bytes_read = count;

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

      struct thread *cur = thread_current();
      lock_acquire(&fs_lock);

      struct file_desc * file_desc = &cur->fd_table[fd];

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || (file_desc->file == NULL && file_desc->dir == NULL))
      {
        lock_release(&fs_lock);
        exit(-1);
      }
      struct file *cur_file = file_desc->file;
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

      const char *filename = buffer_check(f, 0);
      unsigned initial_size = *(unsigned *)(f->esp + 8);

      char *resolved_path = NULL;
      struct dir *resolved_path_cwd = NULL;
      // fs lock before maybe?

      if (!resolve_path((char *)filename, &resolved_path, &resolved_path_cwd))
      {
        free((char *)filename);
        f->eax = 0;
        // printf("no hit create\n");
        break;
      }

      free((char *)filename);

      // printf("resolved path: %s\n", resolved_path);

      if (strlen(resolved_path) > NAME_MAX + 1)
      {
        f->eax = 0;
      }
      else
      {
        lock_acquire(&fs_lock);
        // is this right
        f->eax = filesys_create(resolved_path, initial_size, resolved_path_cwd) ? 1 : 0;
        lock_release(&fs_lock);
      }
      free((char *)resolved_path);
      dir_close(resolved_path_cwd);
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

      struct thread *cur = thread_current();

      /* Open file */
      lock_acquire(&fs_lock);
      struct file *file = filesys_open(filename, cur->curr_dir);
      free((char*)filename);

      /* Check if file exists */
      if (file == NULL) {
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      if (cur->fd_table == NULL)
      {
        cur->fd_table = malloc(FD_MAX * sizeof(struct file_desc));
        if (cur->fd_table == NULL)
        {
          f->eax = -1;
          lock_release(&fs_lock);
          exit(-1);
        }
        for(int i = 0; i<FD_MAX; i++){
          struct file_desc * file_desc = &cur->fd_table[i];
          file_desc->file = NULL;
          file_desc->dir = NULL;
          file_desc->is_dir = false;
        }
      }

      int fd = -1;
      for (int i = FD_MIN; i < FD_MAX; i++) {
        struct file_desc * file_desc = &cur->fd_table[i];
        if (file_desc->file == NULL && file_desc->dir == NULL) {
          fd = i;
          break;
        }
      }

      if (fd ==- 1) {
        file_close(file);
        lock_release(&fs_lock);
        f->eax = -1;
        break;
      }

      struct file_desc * file_desc = &cur->fd_table[fd];
      file_desc->file = file;
      file_desc->is_dir = false;

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

      // go thru readdir make sure not the same as cwd and its .. and . only

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
      struct file_desc * file_desc = &cur->fd_table[fd];

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || (file_desc->file == NULL && file_desc->dir == NULL))
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      /* Get file size */
      f->eax = file_length(file_desc->file);
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
    {
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
    }
    // exec
    case SYS_EXEC:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = -1;
        exit(-1);
      }
      const char *cmd_line = buffer_check(f, -1);
      f->eax = process_execute(cmd_line);
      free((char *)cmd_line);
      break;
    }
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

      struct file_desc * file_desc = &cur->fd_table[fd];

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || (file_desc->file == NULL && file_desc->dir == NULL))
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }
      file_close(file_desc->file);
      file_desc->file = NULL;
      file_desc->dir = NULL;
      file_desc->is_dir = false;

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
      struct thread *cur = thread_current();

      lock_acquire(&fs_lock);

      struct file_desc * file_desc = &cur->fd_table[fd];

      if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || (file_desc->file == NULL && file_desc->dir == NULL))
      {
        f->eax = -1;
        lock_release(&fs_lock);
        exit(-1);
      }

      f->eax = file_tell(file_desc->file);

      lock_release(&fs_lock);
      break;
    }
    // halt
    case SYS_HALT:
      shutdown_power_off();
      break;

    case SYS_CHDIR:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = 0;
        exit(0);
      }
      const char *file = buffer_check(f, 0);
      struct thread *cur = thread_current();
      struct dir *cwd = cur->curr_dir;
      ASSERT(cwd != NULL);

      char *resolved_path = NULL;
      struct dir *resolved_path_cwd = NULL;

      if (!resolve_path((char *)file, &resolved_path, &resolved_path_cwd))
      {
        f->eax = 0;
        free((char *)file);
        // printf("no hit rp chdir\n");
        break;
      }

      free((char *)file);

      ASSERT(resolve_path != NULL);
      ASSERT(resolved_path_cwd != NULL);

      // printf("resolved path: %s\n", resolved_path);

      struct inode *inode = NULL;
      if (!dir_lookup(resolved_path_cwd, resolved_path, &inode))
      {
        dir_close(resolved_path_cwd);
        free(resolved_path);
        f->eax = 0;
        break;
      }

      if (!is_dir(inode))
      {
        inode_close(inode);
        dir_close(resolved_path_cwd);
        free(resolved_path);
        f->eax = 0;
        break;
      }

      struct dir *new_dir = dir_open(inode);
      if (new_dir == NULL)
      {
        dir_close(resolved_path_cwd);
        free(resolved_path);
        f->eax = 0;
        break;
      }

      dir_close(cur->curr_dir);
      cur->curr_dir = new_dir;

      dir_close(resolved_path_cwd);
      free(resolved_path);
      f->eax = 1;

      break;
    }
    case SYS_MKDIR:
    {
      if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      {
        f->eax = 0;
        exit(0);
      }
      const char *file = buffer_check(f, 0);
      struct thread *cur = thread_current();
      struct dir *cwd = cur->curr_dir;
      ASSERT(cwd != NULL);

      char *resolved_path = NULL;
      struct dir *resolved_path_cwd = NULL;

      if (!resolve_path((char *)file, &resolved_path, &resolved_path_cwd))
      {
        f->eax = 0;
        free((char *)file);
        // printf("no hit\n");
        break;
      }

      free((char *)file);

      ASSERT(resolve_path != NULL);
      ASSERT(resolved_path_cwd != NULL);

      // printf("resolved path: %s\n", resolved_path);

      block_sector_t sector = UINT32_MAX - 1;

      if (!free_map_allocate(1, &sector))
      {
        f->eax = 0;
        // printf("no hit freemap\n");
        break;
      }

      ASSERT(sector != UINT32_MAX - 1);

      struct inode *cwd_inode = dir_get_inode(resolved_path_cwd);
      block_sector_t cwd_sector = inode_get_inumber(cwd_inode);

      if (!dir_create(sector, 16, cwd_sector))
      {
        free(resolved_path);
        dir_close(resolved_path_cwd);
        free_map_release(sector, 1);
        f->eax = 0;
        // printf("no hit create\n");
        break;
      }

      if (!dir_add(resolved_path_cwd, resolved_path, sector))
      {
        free(resolved_path);
        dir_close(resolved_path_cwd);
        free_map_release(sector, 1);
        f->eax = 0;
        // printf("no hit add\n");
        break;
      }

      free(resolved_path);
      dir_close(resolved_path_cwd);
      f->eax = 1;
      break;
    }
    case SYS_READDIR:
    {
      // if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4) || !is_valid_user_ptr(f->esp + 8))
      // {
      //   f->eax = 0;
      //   exit(0);
      // }

      // int fd = *((int *)(f->esp + 4));
      // const char *buffer = *((void **)(f->esp + 8));

      // if (!validate_user_buffer((char *)buffer, NAME_MAX + 1)) // same as reaadir macro
      // {
      //   f->eax = 0;
      //   exit(-1);
      // }
      // struct thread *cur = thread_current();

      // lock_acquire(&fs_lock);

      // if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      // {
      //   f->eax = 0;
      //   lock_release(&fs_lock);
      //   exit(-1);
      // }

      // struct inode *inode = inode_reopen(file_get_inode(cur->fd_table[fd]));
      // ASSERT(inode != NULL);
      // // reopen bc dir is closed at the end, don't interfere with other open

      // if (!is_dir(inode))
      // {
      //   f->eax = 0;
      //   lock_release(&fs_lock);
      //   break;
      // }

      // struct dir *dir = get_dir(cur->fd_table[fd]);
      // ASSERT(dir != NULL);
      // if (dir == NULL)
      // {
      //   // printf("dir open fail\n");
      //   f->eax = 0;
      //   lock_release(&fs_lock);
      //   break;
      // }

      // if (!dir_readdir(dir, (char *)buffer))
      // {
      //   // printf("not a dir\n");
      //   dir_close(dir);
      //   f->eax = 0;
      //   lock_release(&fs_lock);
      //   break;
      // }

      // dir_close(dir);
      // lock_release(&fs_lock);
      f->eax = 1;
      break;
    }
    case SYS_ISDIR:
    {
      //  if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      //  {
      //     f->eax = 0;
      //     exit(0);
      //  }

      //  int fd = *((int *)(f->esp + 4));
      //  struct thread *cur = thread_current();

      //  lock_acquire(&fs_lock);

      //  if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || &cur->fd_table[fd] == NULL)
      //  {
      //     f->eax = -1;
      //     lock_release(&fs_lock);
      //     exit(-1);
      //  }

      //  // dir get inode
      //  struct inode *inode = file_get_inode(cur->fd_table[fd]);
      //  ASSERT(inode != NULL);
      //  f->eax = is_dir(inode);
      //  lock_release(&fs_lock);
       break;
    }
    case SYS_INUMBER:
    {
      //  if (!is_valid_user_ptr(f->esp) || !is_valid_user_ptr(f->esp + 4))
      //  {
      //     f->eax = 0;
      //     exit(0);
      //  }

      //  int fd = *((int *)(f->esp + 4));
      //  struct thread *cur = thread_current();

      //  lock_acquire(&fs_lock);

      //  if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || cur->fd_table[fd] == NULL)
      //  {
      //     f->eax = -1;
      //     lock_release(&fs_lock);
      //     exit(-1);
      //  }

      //  struct dir *dir = get_dir(cur->fd_table[fd]);
      //  if (dir == NULL)
      //  {
      //    f->eax = 0;
      //    lock_release(&fs_lock);
      //    break;
      //  }

      //  struct inode *inode = file_get_inode(cur->fd_table[fd]);
      //  ASSERT(inode != NULL);
      //  f->eax = inode_get_inumber(inode);
      //  lock_release(&fs_lock);
       break;
    }
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
      int buffer_size = (size-count) > BLOCK_SECTOR_SIZE ? BLOCK_SECTOR_SIZE : size-count;

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
    struct file_desc * file_desc = &cur->fd_table[fd];

    if (fd < FD_MIN || fd >= FD_MAX || cur->fd_table == NULL || (file_desc->file == NULL && file_desc->dir == NULL))
    {
      lock_release(&fs_lock);
      exit(-1);
    }
    struct file *cur_file = file_desc->file;

    unsigned count = 0;
    while(count < size){
      int buffer_size = (size-count) > BLOCK_SECTOR_SIZE ? BLOCK_SECTOR_SIZE : size-count;

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

  printf("%s: exit(%d)\n", thread_curr->ps->user_prog_name, status);

  lock_acquire(&ps->ps_lock);
  ps->exit_status = status;
  lock_release(&ps->ps_lock);

  thread_exit();
}