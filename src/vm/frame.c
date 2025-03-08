#include <lib/kernel/list.h>
#include <threads/synch.h>
#include <vm/page.h>
#include <stdbool.h>

// enables page fault handling by supplementing the page table
static struct frame_table {
    struct list used_list; // used pages
    struct list free_list; // free pages
    struct lock lock; //lock to protect lists

    struct list_elem clock_elem; // clock hand
};

struct frame {
    void * kaddr; // kernel/phys addr
    struct page * page; // back pointer to page in SPT
    struct list_elem elem; //list elem
    bool pinned; // eviction, pinning
};