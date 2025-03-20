#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "stdbool.h"
// #include "threads/palloc.h"
#include "threads/thread.h"
// #include "userprog/pagedir.h"
// #include "threads/vaddr.h"
// #include <stdlib.h>



// enables page fault handling by supplementing the page table


struct frame {
    void * kaddr; // kernel/phys addr
    struct thread* thread; //given by caller
    struct page * page; // back pointer to page in SPT //given by caller
    struct list_elem elem; //list elem
    bool pinned; // eviction, pinning //given by caller
};


void init_ft(void);
struct frame* ft_get_page_frame(struct thread*, struct page * page, bool);
void page_frame_freed(struct frame * frame);
struct frame * get_page_frame(struct page * page);


#endif