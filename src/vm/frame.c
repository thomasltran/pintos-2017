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
#include <stdio.h>
#include "userprog/syscall.h"
//frame table
//static because frame.c should use it directly while other compilation units should utilzie frame table functions
struct frame_table* ft;
static struct frame * get_next_frame(struct frame *);
// static void * evict_frame(struct thread *, void *, bool);
static void * evict_frame(void);

void init_ft(void) {
    ft = malloc(sizeof(struct frame_table));
    list_init(&ft->used_list);
    list_init(&ft->free_list);
    lock_init(&ft->lock);
    // Initialize clock element to head of used list
    ft->clock_elem = list_head(&ft->used_list);

    uint32_t* kpage;

    while((kpage = palloc_get_page(PAL_USER | PAL_ZERO))){
        struct frame * frame_ptr = malloc(sizeof(struct frame));
        frame_ptr->kaddr = kpage;
        list_push_front(&ft->free_list, &frame_ptr->elem);
    }
}

void* ft_get_page(struct thread* page_thread, void* u_vaddr, bool pinned){
    void *kpage = NULL;

    // primitive implementation
    if(!list_empty(&ft->free_list)){
        struct list_elem * e = list_pop_front(&ft->free_list);
        struct frame * frame_ptr = list_entry(e, struct frame, elem);
        frame_ptr->thread = page_thread;
        frame_ptr->page = find_page(page_thread->supp_pt, u_vaddr);
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, e);
        return frame_ptr->kaddr;
    }
    else {
        // no free frames, we must evict a frame
        // kpage = evict_frame(page_thread, u_vaddr, pinned);
        kpage = evict_frame();

        if (kpage != NULL) { // if we successfully evicted a frame
            struct frame *frame_ptr = list_entry(list_next(list_begin(&ft->used_list)), struct frame, elem); // get the next frame in the clock hand order
            frame_ptr->thread = page_thread; // set the thread
            frame_ptr->page = find_page(page_thread->supp_pt, u_vaddr); // find the page
            frame_ptr->pinned = pinned; // set the pinned status
            list_push_front(&ft->used_list, &frame_ptr->elem); // add to the used list
            
            // Reset accessed and dirty bits for kernel virtual address
            pagedir_set_accessed(page_thread->pagedir, kpage, false);
            pagedir_set_dirty(page_thread->pagedir, kpage, false);
        }
    }
    return kpage;
}

void destroy_frame_table(){
    
    while(!list_empty(&ft->free_list)){
        struct list_elem* e = list_pop_front(&ft->free_list);
        struct frame* frame_ptr = list_entry(e, struct frame, elem);
        free(frame_ptr);
    }
    while(!list_empty(&ft->used_list)){
        struct list_elem* e = list_pop_front(&ft->used_list);
        struct frame* frame_ptr = list_entry(e, struct frame, elem);
        free(frame_ptr);
    }

    free(ft);
}

/* - - - - - - - - - - Helper Functions For Eviction - - - - - - - - - - */

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
// static void *
// evict_frame(UNUSED struct thread *page_thread, UNUSED void *u_vaddr, UNUSED bool pinned) {
static void *
evict_frame() {

    ASSERT(lock_held_by_current_thread(&vm_lock));

    // validation for clock hand
    if(ft->clock_elem == NULL || 
       ft->clock_elem == list_head(&ft->used_list) || 
       ft->clock_elem == list_tail(&ft->used_list)) {
        ft->clock_elem = list_begin(&ft->used_list);
    }

    struct frame *victim = NULL;
    struct frame *clock_start = list_entry(ft->clock_elem, struct frame, elem);
    struct frame *curr = clock_start;

    // loop through the frame table until victim is found
    while (victim == NULL) {
        // skip pinned frames
        if (curr->pinned) {
            curr = get_next_frame(curr);
            continue;
        }
        
        // get the page table entries for user address
        uint32_t *pd = curr->thread->pagedir;
        void *upage = curr->page->uaddr;

        // check if the frame has been accessed
        bool accessed = pagedir_is_accessed(pd, upage);

        if (!accessed) {
            // found a non recently accessed frame to evict
            victim = curr;
        } else {
            // clear the accessed bit and move to next frame
            pagedir_set_accessed(pd, upage, false);
            curr = get_next_frame(curr);
        }

        // if we've looped through the entire frame table with no victim found, reset the clock hand
        if (curr == clock_start && victim == NULL) {
            curr = get_next_frame(curr);
        }
    } // end while loop

    // update clock hand for next eviction
    ft->clock_elem = &get_next_frame(victim)->elem;

    // handle victim based on its type
    bool dirty = pagedir_is_dirty(victim->thread->pagedir, victim->page->uaddr); 

    switch (victim->page->page_status) {

        // TODO: Might have to add more cases here

        case CODE:
            break; // no need to write back code pages (just a read-only page)
        // DATA, BSS, STACK: write to swap if dirty
        case DATA_BSS:
        case STACK:
            // victim->page->swap_index = swap_out(victim->kaddr);
            victim->page->page_location = SWAP; // update status to show in swap
            break;
        // MMAP: write back to file if dirty
        case MMAP:
            if (dirty) {
                // write back to file if dirty
                file_write_at(victim->page->file, victim->kaddr, victim->page->read_bytes, victim->page->ofs);
            }
            break;
        default:
            break;
    } // end switch

    // Reset bits before clearing page
    pagedir_set_accessed(victim->thread->pagedir, victim->page->uaddr, false);
    pagedir_set_dirty(victim->thread->pagedir, victim->page->uaddr, false);
    
    // then clear the page
    pagedir_clear_page(victim->thread->pagedir, victim->page->uaddr);

    // clear frame data but keep the frame sturcture
    void *kaddr = victim->kaddr;
    victim->page = NULL;
    victim->thread = NULL;
    
    return kaddr;
}