#include "lib/kernel/list.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "stdbool.h"
#include "frame.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/mappedfile.h"
#include <stdio.h>
#include "userprog/syscall.h"
#include "vm/swap.h"

// frame table
struct frame_table {
    struct list used_list; // used pages
    struct list free_list; // free pages

    struct list_elem* clock_elem; // clock hand
};

// global frame table
static struct frame_table * ft;

static struct frame * get_next_frame(struct frame *);
static struct frame *evict_frame(void);

// init frame table
void init_ft(void) {
    ft = malloc(sizeof(struct frame_table));
    if(ft == NULL){
        exit(-1);
    }
    list_init(&ft->used_list);
    list_init(&ft->free_list);

    ft->clock_elem = NULL;

    uint32_t* kpage;

    // pre-fetch all the pages from user pool
    while((kpage = palloc_get_page(PAL_USER | PAL_ZERO))){
        struct frame * frame_ptr = malloc(sizeof(struct frame));
        frame_ptr->kaddr = kpage;
        frame_ptr->pinned = false;
        list_push_front(&ft->free_list, &frame_ptr->elem);
    }
}

// get a page frame
struct frame *ft_get_page_frame(struct thread *page_thread, struct page * page, bool pinned)
{
    struct frame * frame_ptr = NULL;

    // get from free list if we can
    if(!list_empty(&ft->free_list)){
        struct list_elem * e = list_pop_front(&ft->free_list);
        frame_ptr = list_entry(e, struct frame, elem);
        frame_ptr->thread = page_thread;
        frame_ptr->page = page;
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, e);
        return frame_ptr;
    }

    // need to evict a page frame
    frame_ptr = evict_frame();
    ASSERT(frame_ptr != NULL);

    if (frame_ptr != NULL) {
        frame_ptr->thread = page_thread;
        frame_ptr->page = page;
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, &frame_ptr->elem);
    }
    return frame_ptr;
}

// Get the next frame in the clock hand order
static struct frame *
get_next_frame(struct frame *current) {
    struct list_elem *next = list_next(&current->elem);
    if (next == list_end(&ft->used_list)) {
        next = list_begin(&ft->used_list); // wrap around to the beginning of the list
    }
    return list_entry(next, struct frame, elem);
}

// Evict a frame from the frame table (using clock hand algorithm)
static struct frame *
evict_frame()
{
    ASSERT(lock_held_by_current_thread(&vm_lock));

    // first time we evict
    if(ft->clock_elem == NULL){
        ft->clock_elem = list_begin(&ft->used_list);
    }

    struct frame *victim = NULL;
    struct frame *curr = list_entry(ft->clock_elem, struct frame, elem);
    ASSERT(curr != NULL);

    while (victim == NULL) {
        ASSERT(curr->page != NULL && curr->page->page_location == PAGED_IN && curr->thread->pagedir != NULL); // used list attributes
        
        // skip if pinned
        if (curr->pinned == true) {
            curr = get_next_frame(curr);
            continue;
        }

        // check if the frame has been accessed
        bool accessed = pagedir_is_accessed(curr->thread->pagedir, curr->page->uaddr);

        // trail is dirty, get our victim to evict
        if (!accessed) {
            victim = curr;
            victim->pinned = true;
            victim->page->page_location = IN_TRANSIT;

            ASSERT(victim->thread->pagedir != NULL);

            // clear the accessed bit, not present
            pagedir_clear_page(victim->thread->pagedir, pg_round_down(victim->page->uaddr));

            // set clock hand
            ft->clock_elem = &get_next_frame(victim)->elem;
            list_remove(&victim->elem);

            break;
        } else {
            // rake the trail
            pagedir_set_accessed(curr->thread->pagedir, curr->page->uaddr, false);
            curr = get_next_frame(curr);
            continue;
        }
    }

    switch (victim->page->page_status) {
        // no need to write back code pages (just a read-only page)
        case CODE:
            victim->page->page_location = PAGED_OUT;
            break;

        // DATA, BSS, STACK: write to swap if dirty
        case DATA_BSS:
        case STACK:
            victim->page->page_location = SWAP;
            ASSERT(victim->page->swap_index == UINT32_MAX);

            victim->page->swap_index = st_write_at(victim->kaddr);
            break;

        // MMAP: wb to file if dirty
        case MUNMAP: // won't hit here
        case MMAP:
            if (pagedir_is_dirty(victim->thread->pagedir, victim->page->uaddr)) {
                struct mapped_file * mapped_file = find_mapped_file(victim->thread->mapped_file_table, victim->page->map_id);
                ASSERT(mapped_file != NULL);

                lock_release(&vm_lock);
                lock_acquire(&fs_lock);
                
                file_write_at(mapped_file->file, victim->kaddr, victim->page->read_bytes, victim->page->ofs);
                
                lock_release(&fs_lock);
                lock_acquire(&vm_lock);
            }
            victim->page->page_location = PAGED_OUT;

            break;
        default:
            victim->page->page_location = PAGED_OUT;
            break;
    }
    cond_broadcast(&victim->page->transit, &vm_lock); // for eviction

    victim->thread = NULL;
    victim->page = NULL;

    // frame fields set after return
    return victim;
}

// used page frame added to free list
void page_frame_freed(struct frame * frame){
    list_remove(&frame->elem);
    ASSERT(frame->thread->pagedir != NULL);
    ASSERT(frame->page != NULL);

    uint32_t * pd = frame->thread->pagedir;
    frame->thread = NULL;

    pagedir_clear_page(pd, pg_round_down(frame->page->uaddr));
    struct page * page = frame->page;
    page->page_location = PAGED_OUT;
    frame->page = NULL;
    frame->pinned = false;
    list_push_front(&ft->free_list, &frame->elem);
}

// get page frame corresponding to a page
struct frame * get_page_frame(struct page * page){
    for (struct list_elem *e = list_begin(&ft->used_list); e != list_end(&ft->used_list); e = list_next(e)){
        struct frame *frame = list_entry(e, struct frame, elem);

        ASSERT(frame->page != NULL);

        if(frame->page == page){
            return frame;
        }
    }
    return NULL;
}
