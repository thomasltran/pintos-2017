#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "threads/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "lib/string.h"
#include <stdbool.h>
#include "threads/pte.h"
#include "vm/frame.h"
#include "vm/swap.h"
/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
static bool
install_page (void *upage, void *kpage, bool writable);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present UNUSED;  /* True: not-present page, false: writing r/o page. */
  bool write UNUSED;        /* True: access was write, false: access was read. */
  bool user UNUSED;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
//   printf ("Page fault at %p: %s error %s page in %s context.\n",
//           fault_addr,
//           not_present ? "not present" : "rights violation",
//           write ? "writing" : "reading",
//           user ? "user" : "kernel");

//printf("fault addr %p fesp %p thread esp %p\n", fault_addr, f->esp, thread_current()->esp);
#ifdef VM
   if (fault_addr < PHYS_BASE)
   {
      void * esp = NULL;
      struct thread *thread_cur = thread_current();

      // saved thread_cur->esp at the beginning of syscall, null at the end or exit
      if(thread_cur->esp != NULL){
         esp = thread_cur->esp;
      }
      else {
         esp = f->esp;
      }

      // printf("fault addr %p fesp %p thread esp %p esp %p\n", fault_addr, f->esp, thread_current()->esp, esp);
      // printf("minus %p\n", esp - 32);

      bool stack_growth = false;
    
      if (fault_addr >= esp - 32 && fault_addr > PHYS_BASE - STACK_LIMIT)
          stack_growth = true;

      lock_acquire(&vm_lock);

      struct page *fault_page = NULL;

      if (stack_growth)
      {
         //printf("fault addr %p fesp %p thread esp %p\n", fault_addr, f->esp, thread_current()->esp);

         struct page *page = create_page(fault_addr, NULL, 0, 0, PGSIZE, true, STACK, PAGED_OUT);
         if (page == NULL)
         {
            lock_release(&vm_lock);
            f->eax = -1;
            exit(-1);
         }

         ASSERT(thread_current()->supp_pt != NULL)
         struct hash_elem *ret = hash_insert(&thread_current()->supp_pt->hash_map, &page->hash_elem);
         ASSERT(ret == NULL);
      }

      fault_page = find_page(thread_cur->supp_pt, fault_addr);
      if (fault_page == NULL)
      {
         //printf("pf couldn't find\n");
         lock_release(&vm_lock);
         f->eax = -1;
         exit(-1);
      }
      if(fault_page->page_status == MUNMAP){
         lock_release(&vm_lock);
         f->eax = -1;
         exit(-1);
      }

      if(write && !fault_page->writable){
         lock_release(&vm_lock);
         f->eax = -1;
         exit(-1);
      }

      void *upage = pg_round_down(fault_addr);

      ASSERT(pagedir_get_page(thread_cur->pagedir, upage) == NULL);

      // uint8_t *kpage = palloc_get_page(PAL_USER);
      struct frame * frame = ft_get_page_frame(thread_current(), fault_page, true);
      uint8_t *kpage = frame->kaddr;
      // can get evicted here, i think we need to pin
      // dont want eviction before file_read
      ASSERT(kpage != NULL);
      if (kpage == NULL)
      {
         printf("failed1\n");
         lock_release(&vm_lock);
         f->eax = -1;
         exit(-1);
      }
      // evict (file write) -> pf -> get_frame

      /* Load this page. */

      ASSERT(pg_round_down(fault_addr) == pg_round_down(fault_page->uaddr));

      fault_page->frame = frame;
      bool in_swap = fault_page->page_location == SWAP;
      fault_page->page_location = PAGED_IN; // if it exits in the chunk after, free_page needs to be able to free it
      // pinned so it won't get evicted

      if (pagedir_get_page(thread_cur->pagedir, upage) != NULL || pagedir_set_page(thread_cur->pagedir, upage, kpage, fault_page->writable) == false)
      {
         printf("failed2\n");
         page_frame_freed(frame);
         lock_release(&vm_lock);
         f->eax = -1;
         exit(-1);
      }

      if (!stack_growth)
      {
         lock_release(&vm_lock);
         lock_acquire(&fs_lock);
         if((fault_page->page_status == DATA_BSS || fault_page->page_status == STACK) && in_swap){
            ASSERT(fault_page->swap_index != UINT32_MAX);

            st_read_at(fault_page->uaddr, fault_page->swap_index);
            fault_page->swap_index = UINT32_MAX; // paged back in
         }
         else{
            file_seek(fault_page->file, fault_page->ofs);
            if (file_read(fault_page->file, kpage, fault_page->read_bytes) != (int)fault_page->read_bytes)
            {
               printf("failed3\n");
               lock_release(&fs_lock);
               f->eax = -1;
               exit(-1);
            }   
            
         }
         lock_release(&fs_lock);
         lock_acquire(&vm_lock);
      }
      memset(kpage + fault_page->read_bytes, 0, fault_page->zero_bytes);
      frame->pinned = false;
      //printf("made it past\n");
      //check_used();

      lock_release(&vm_lock);
   }
   else
   {
      //   kill(f);
      f->eax = -1;
      exit(-1);
   }

#else
   if (user)
   {
      f->eax = -1;
      exit(-1);
   }
#endif
}

/*
keep track of this info in ds (for ucode eviction is free (r-only, alr on disc), data/bss/ustack (need to find swap space to write it to, mmap write to file)
*/
UNUSED static bool
install_page(void *upage, void *kpage, bool writable)
{
   struct thread *t = thread_current();

   /* Verify that there's not already a page at that virtual
      address, then map our page there. */
   return (pagedir_get_page(t->pagedir, upage) == NULL && pagedir_set_page(t->pagedir, upage, kpage, writable));
}

   // /* Get a page of memory. */

   // /* Add the page to the process's address space. */
   // if (!install_page (upage, kpage, writable))
   //   {
   //     palloc_free_page (kpage);
   //     return false;
   //   }

   //   struct thread *t = thread_current ();

   //   /* Verify that there's not already a page at that virtual
   //      address, then map our page there. */
   //   return (pagedir_get_page (t->pagedir, upage) == NULL
   //           && pagedir_set_page (t->pagedir, upage, kpage, writable));