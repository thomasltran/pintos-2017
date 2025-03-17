#include "lib/kernel/list.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "stdbool.h"
// #include "threads/palloc.h"
#include "threads/thread.h"
// #include "userprog/pagedir.h"
// #include "threads/vaddr.h"
// #include <stdlib.h>


#ifndef VM_FRAME_H
#define VM_FRAME_H
// enables page fault handling by supplementing the page table
struct frame_table {
    struct list used_list; // used pages
    struct list free_list; // free pages
    struct lock lock; //lock to protect lists

    struct list_elem clock_elem; // clock hand
};

struct frame {
    void * kaddr; // kernel/phys addr
    struct thread* thread; //given by caller
    struct page * page; // back pointer to page in SPT //given by caller
    struct list_elem elem; //list elem
    bool pinned; // eviction, pinning //given by caller
};


void init_ft(void);
void destroy_frame_table(void);
void* ft_get_page(struct thread*, void*, bool);

#endif