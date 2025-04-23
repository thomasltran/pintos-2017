#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include "threads/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "lib/stdio.h"
#include "lib/string.h"

static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, struct parent_child *parent_child, char **argv, int argc, void (**eip)(void), void **esp);

/* Starts a new thread running a user program loaded from file_name.
   Creates a process struct to track the parent-child relationship and 
   synchronize between parent and child threads. The new thread may be 
   scheduled (and may even exit) before process_execute() returns.
   Returns the new process's thread id, or TID_ERROR if the thread 
   cannot be created or if process struct allocation fails. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  struct parent_child *parent_child = malloc(sizeof(struct parent_child));
  if (parent_child == NULL)
  {
    return TID_ERROR;
  }
  lock_init(&parent_child->parent_child_lock);
  parent_child->exit_status = -1;
  parent_child->child_tid = -1; // remains -1 if failed start
  parent_child->ref_count = 2;
  parent_child->good_start = false;
  parent_child->exe_file = NULL;
  sema_init(&parent_child->user_prog_exit, 0);
  sema_init(&parent_child->child_started, 0);

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
  {
    free(parent_child);
    return TID_ERROR;
  }
  strlcpy (fn_copy, file_name, PGSIZE);
  parent_child->user_prog_name = fn_copy;

  // child of the calling process
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(file_name, NICE_DEFAULT, start_process, parent_child);
  
  sema_down(&parent_child->child_started); // ensure child starts

  if (tid == TID_ERROR || !parent_child->good_start) // if child thread fails, never gets added to the list
  {
    free(parent_child);
    return TID_ERROR;
  }

  parent_child->child_tid = tid; // if child is in process_exit before this, it has the ref to parent_child already so semaphore is good
  ASSERT(parent_child->user_prog_name != NULL);

  struct thread *parent_thread = thread_current();
  list_push_back(&parent_thread->parent_child_list, &parent_child->elem);

  return tid;
}

/* A thread function that loads a user process and starts it running.
   Parses the command line arguments from file_name into argv array.
   Sets up the initial interrupt frame and loads the executable.
   Frees the file_name page when done.
   If load fails, terminates the thread. */
static void
start_process(void *p)
{
  struct parent_child *parent_child = (struct parent_child *)p;
  char *file_name = parent_child->user_prog_name;

  char *argv[64]; // find better limit
  char *token, *save_ptr;
  int i = 0;

  for (token = strtok_r(file_name, " ", &save_ptr); token != NULL;)
  {
    while (*token == ' ') // space check
      token++;
    argv[i++] = token;
    token = strtok_r(NULL, " ", &save_ptr);
  }
  argv[i + 1] = NULL;
  file_name = argv[0];

  char *hold = malloc(sizeof(char *));
  if (hold == NULL)
  {
    thread_exit();
  }
  int name_length = 0;
  while (argv[0][name_length] != '\0')
  {
    name_length++;
  }
  strlcpy(hold, argv[0], name_length + 1);

  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load(file_name, parent_child, argv, i, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page(parent_child->user_prog_name);

  if (!success)
  {
    free(hold);
    sema_up(&parent_child->child_started);
    thread_exit(); // never returns to caller
  }
  else
  {
    parent_child->good_start = true;
    struct thread *child_thread = thread_current();
    child_thread->parent_child = parent_child;
    child_thread->parent_child->user_prog_name = hold;
    sema_up(&parent_child->child_started);
  }

  // hex_dump(if_.esp, &if_.esp, 0xc0000000, 1);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(tid_t child_tid)
{
  struct thread *parent_thread = thread_current();
  int exit_status = -1;

  for (struct list_elem *e = list_begin(&parent_thread->parent_child_list);
       e != list_end(&parent_thread->parent_child_list);)
  {
    struct parent_child *parent_child = list_entry(e, struct parent_child, elem);
    ASSERT(parent_child != NULL);

    lock_acquire(&parent_child->parent_child_lock);
    
    if (child_tid == parent_child->child_tid && parent_child->ref_count > 0) // HB relationship for parent in setting vs. reading parent_child->child_tid
    {
      lock_release(&parent_child->parent_child_lock);

      sema_down(&parent_child->user_prog_exit);

      lock_acquire(&parent_child->parent_child_lock);

      exit_status = parent_child->exit_status;

      parent_child->ref_count--;
      ASSERT(parent_child->ref_count >= 0);
      if (parent_child->ref_count == 0) // nothing waiting for it
      {
        list_remove(&parent_child->elem); // list oparent_child only done by parent
        lock_release(&parent_child->parent_child_lock);
        free(parent_child->user_prog_name);
        free(parent_child);
        return exit_status;
      }

      lock_release(&parent_child->parent_child_lock);
      return exit_status;
    }
    lock_release(&parent_child->parent_child_lock);

    e = list_next(e);
  }
  return -1;
}

/* Free the current process's resources. */
void
process_exit(void)
{
  struct thread *cur = thread_current();
  uint32_t *pd;
  int tid = cur->tid;

  for (struct list_elem *e = list_begin(&cur->parent_child_list);
       e != list_end(&cur->parent_child_list);)
  {
    struct parent_child *parent_child = list_entry(e, struct parent_child, elem);
    ASSERT(parent_child != NULL);

    lock_acquire(&parent_child->parent_child_lock);

    parent_child->ref_count--;

    ASSERT(parent_child->ref_count >= 0);
    if (parent_child->ref_count == 0)
    {
      e = list_remove(e);
      lock_release(&parent_child->parent_child_lock);

      free(parent_child->user_prog_name);
      free(parent_child);
    }
    else
    {
      e = list_next(e);
      lock_release(&parent_child->parent_child_lock);
    }
  }

  struct parent_child *parent_child = cur->parent_child;

  if(parent_child != NULL && parent_child->exe_file != NULL){
    lock_acquire(&fs_lock);
    file_close(parent_child->exe_file);
    lock_release(&fs_lock);
  }

  // clean up fd's when a thread exits
  lock_acquire(&fs_lock);
  struct file **fd_table = cur->pcb->fd_table;
  if (fd_table != NULL)
  { // called open
    for (int i = FD_MIN; i < FD_MAX; i++)
    {
      if(fd_table[i] != NULL){
        file_close(fd_table[i]);
      }
    }
    free(fd_table);
  }
  lock_release(&fs_lock);

  if (parent_child != NULL)
  {
    lock_acquire(&parent_child->parent_child_lock);

    parent_child->ref_count--;

    ASSERT(parent_child->ref_count >= 0);
    if (parent_child->ref_count == 0) // nothing waiting for it
    {
      list_remove(&parent_child->elem); // list oparent_child only done by parent
      lock_release(&parent_child->parent_child_lock);
      free(parent_child->user_prog_name);
      free(parent_child);
    }
    else
    {
      sema_up(&parent_child->user_prog_exit);

      lock_release(&parent_child->parent_child_lock);
    }
  }

  /* Destroy the  current process's page directory and switch back
     to the kernel-only page directory. */

  ASSERT(tid == thread_current()->tid);
  pd = cur->pcb->pagedir;
  if (pd != NULL)
  {
    // should auto clean up the last thread's 8mb chunk if not main thread too
    /* Correct ordering here is crucial.  We must set
       cur->pcb->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pcb->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pcb->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32   /*  Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /*  Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /*  Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /*  Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Pushes the command line arguments onto the stack in the correct order,
   ensuring proper word alignment (4 byte boundaries).
   Returns true if successful, false otherwise. */
bool load(const char *file_name, struct parent_child *parent_child, char **argv, int argc, void (**eip)(void), void **esp)
{
  struct thread *t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char *addr[argc];
  int addr_i = 0;

  /* Allocate and activate page directory. */
  t->pcb->pagedir = pagedir_create();
  if (t->pcb->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  lock_acquire(&fs_lock);
  file = filesys_open(file_name);
  if (file == NULL)
  {
    parent_child->exe_file = NULL;
    lock_release(&fs_lock);
    goto done;
  }
  else
  {
    parent_child->exe_file = file;
    file_deny_write(parent_child->exe_file);
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024)
  {
    lock_release(&fs_lock);
    goto done;
  }
  lock_release(&fs_lock);

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
  {
    struct Elf32_Phdr phdr;
    lock_acquire(&fs_lock);

    if (file_ofs < 0 || file_ofs > file_length(file))
    {
      lock_release(&fs_lock);
      goto done;
    }
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
    {
      lock_release(&fs_lock);
      goto done;
    }
    lock_release(&fs_lock);

    file_ofs += sizeof phdr;
    switch (phdr.p_type)
    {
    case PT_NULL:
    case PT_NOTE:
    case PT_PHDR:
    case PT_STACK:
    default:
      /* Ignore this segment. */
      break;
    case PT_DYNAMIC:
    case PT_INTERP:
    case PT_SHLIB:
      goto done;
    case PT_LOAD:
      if (validate_segment(&phdr, file))
      {
        bool writable = (phdr.p_flags & PF_W) != 0;
        uint32_t file_page = phdr.p_offset & ~PGMASK;
        uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
        uint32_t page_offset = phdr.p_vaddr & PGMASK;
        uint32_t read_bytes, zero_bytes;
        if (phdr.p_filesz > 0)
        {
          /* Normal segment.
            Read initial part from disk and zero the rest. */
          read_bytes = page_offset + phdr.p_filesz;
          zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
        }
        else
        {
          /* Entirely zero.
            Don't read anything from disk. */
          read_bytes = 0;
          zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
        }
        if (!load_segment(file, file_page, (void *)mem_page,
                          read_bytes, zero_bytes, writable))
          goto done;
      }
      else
        goto done;
      break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  for (i = argc - 1; i >= 0; i--) // push words onto stack, reversed
  {
    int length = strnlen(argv[i], 1024) + 1; // for \0
    *esp -= length;
    addr[addr_i++] = (char *)(*esp);
    
    for (int k = 0; k < length; k++)
    {
      *((char *)(*esp) + k) = argv[i][k];
    }
  }

  // word-align
  /* Align stack pointer to 4 byte boundary */
  uintptr_t esp_int = (uintptr_t)*esp;
  int padding = esp_int % 4;
  if (padding != 0)
  {
    esp_int -= padding;
    *esp = (void *)esp_int;
    memset(*esp, 0, padding); // zero padding bytes
  }

  *esp -= sizeof(char *); // null sentinel
  *((char **)(*esp)) = NULL;

  for(i = 0; i < addr_i; i++){ // push adress of each string
    *esp -= sizeof(char *);
    *((char **)(*esp)) = addr[i];
  }

  char ** curr_esp = (char **)(*esp); // argv
  *esp -= sizeof(char **);
  *((char ***)(*esp)) = curr_esp;

  *esp -= sizeof(int); // argc
  *((int*)(*esp)) = argc;

  *esp -= sizeof(void *); // ret addr
  *((void **)*esp) = 0;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  /* Debug: Show stack contents 
  hex_dump(*esp, *esp, PHYS_BASE - (uintptr_t)*esp, true);
  */
  success = true;

  done:
  /* We arrive here whether the load is successful or not. */
  // file_close(file);
  return success;
}
   

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE; // when can we revert this?
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pcb->pagedir, upage) == NULL
          && pagedir_set_page (t->pcb->pagedir, upage, kpage, writable));
}
