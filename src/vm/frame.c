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
//frame table
//static because frame.c should use it directly while other compilation units should utilzie frame table functions
struct frame_table {
    struct list used_list; // used pages
    struct list free_list; // free pages
    // struct lock lock; //lock to protect lists

    struct list_elem* clock_elem; // clock hand
};

static struct frame_table * ft;

static struct frame * get_next_frame(struct frame *);
// static void * evict_frame(struct thread *, void *, bool);
static struct frame *evict_frame(void);

void init_ft(void) {
    ft = malloc(sizeof(struct frame_table));
    if(ft == NULL){
        exit(-1);
    }
    list_init(&ft->used_list);
    list_init(&ft->free_list);
    // lock_init(&ft->lock);
    // Initialize clock element to head of used list
    ft->clock_elem = list_head(&ft->used_list);

    uint32_t* kpage;

    while((kpage = palloc_get_page(PAL_USER | PAL_ZERO))){
        struct frame * frame_ptr = malloc(sizeof(struct frame));
        frame_ptr->kaddr = kpage;
        list_push_front(&ft->free_list, &frame_ptr->elem);
    }
}

void page_frame_freed(struct frame * frame){
    ASSERT(frame->thread->pagedir != NULL);
    pagedir_clear_page(frame->thread->pagedir, frame->page->uaddr); // do we need this?
    pagedir_set_accessed(frame->thread->pagedir, frame->kaddr, false);
    pagedir_set_dirty(frame->thread->pagedir, frame->kaddr, false);

    frame->thread = NULL;
    list_remove(&frame->elem);
    list_push_front(&ft->free_list, &frame->elem);
    frame->page = NULL;
    frame->pinned = false;
}

struct frame *ft_get_page_frame(struct thread *page_thread, struct page * page, bool pinned)
{
    struct frame * frame_ptr = NULL;
    // primitive implementation
    if(!list_empty(&ft->free_list)){
        struct list_elem * e = list_pop_front(&ft->free_list);
        struct frame * frame_ptr = list_entry(e, struct frame, elem);
        frame_ptr->thread = page_thread;
        frame_ptr->page = page;
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, e);
        return frame_ptr;
    }

    // no free frames, we must evict a frame
    // kpage = evict_frame(page_thread, u_vaddr, pinned);
    frame_ptr = evict_frame();
    ASSERT(frame_ptr != NULL);

    if (frame_ptr != NULL) { // if we successfully evicted a frame
        struct frame *frame_ptr = list_entry(list_next(list_begin(&ft->used_list)), struct frame, elem); // get the next frame in the clock hand order
        frame_ptr->thread = page_thread; // set the thread
        frame_ptr->page = page;
        frame_ptr->pinned = pinned; // set the pinned status
        list_push_front(&ft->used_list, &frame_ptr->elem); // add to the used list
        
        // Reset accessed and dirty bits for kernel virtual address
        // we shouldn't need these?
        pagedir_set_accessed(page_thread->pagedir, frame_ptr->kaddr, false);
        pagedir_set_dirty(page_thread->pagedir, frame_ptr->kaddr, false);
    }
    return frame_ptr;
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
static struct frame *
evict_frame()
{
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

    // struct supp_pt *victim_spt = NULL;
    struct mapped_file_table *victim_mapped_file_table = NULL;

    // loop through the frame table until victim is found
    while (victim == NULL) {
        // skip pinned frames
        if (curr->pinned) {
            curr = get_next_frame(curr);
            continue;
        }
        
        // get the page table entries for user address
        ASSERT(curr->thread->pagedir != NULL);
        uint32_t *pd = curr->thread->pagedir;
        void *upage = curr->page->uaddr;

        // check if the frame has been accessed
        bool accessed = pagedir_is_accessed(pd, upage);

        if (!accessed) {
            // found a non recently accessed frame to evict
            victim = curr;
            // victim_spt = victim->thread->supp_pt;
            victim_mapped_file_table = victim->thread->mapped_file_table;
            victim->pinned = true; // for write back if applicable
            victim->thread = NULL; // break mapping (in transit page)
            break;
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

    // printf("ptr is %p\n", victim->page->uaddr); // we get a weird ptr
    //printf("pre dirty check\n");
    ASSERT(victim->thread->pagedir != NULL);
    ASSERT(victim->page != NULL);
    bool dirty = pagedir_is_dirty(victim->thread->pagedir, victim->page->uaddr); 
    //printf("post dirty check\n");

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
                // wrong
                // do we write just the evicted page back to file, or the entirety of the buffer mmap spans? will this be handled on a eviction by eviction basis?
                struct mapped_file *mapped_file = NULL;

                for (struct list_elem *e = list_begin(&victim_mapped_file_table->list); e != list_end(&victim_mapped_file_table->list); e = list_next(e))
                {
                    mapped_file = list_entry(e, struct mapped_file, elem);

                    ASSERT(victim->page->map_id != -1);
                    if (mapped_file->map_id == victim->page->map_id)
                    {
                        break;
                    }
                    mapped_file = NULL;
                }

                ASSERT(mapped_file != NULL);

                lock_release(&vm_lock);
                lock_acquire(&fs_lock);

                file_write_at(mapped_file->file, victim->page->uaddr, victim->page->read_bytes, victim->page->ofs);

                lock_release(&fs_lock);
                lock_acquire(&vm_lock);
            }
            break;
        default:
            break;
    } // end switch

    victim->pinned = false; // reset for writeback, if applicable

    // Reset bits before clearing page
    // pagedir_set_accessed(victim->thread->pagedir, victim->page->uaddr, false);
    // pagedir_set_dirty(victim->thread->pagedir, victim->page->uaddr, false);

    // then clear the page
    pagedir_clear_page(victim->thread->pagedir, victim->page->uaddr);

    // clear frame data but keep the frame sturcture
    victim->page = NULL;
    // victim->thread = NULL;

    return victim;
}

// are we responsible for callling free_page except for process cleanup?