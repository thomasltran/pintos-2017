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

    ft->clock_elem = NULL;

    uint32_t* kpage;

    while((kpage = palloc_get_page(PAL_USER | PAL_ZERO))){
        struct frame * frame_ptr = malloc(sizeof(struct frame));
        frame_ptr->kaddr = kpage;
        frame_ptr->pinned = false;
        list_push_front(&ft->free_list, &frame_ptr->elem);
    }
}

struct frame *ft_get_page_frame(struct thread *page_thread, struct page * page, bool pinned)
{
    struct frame * frame_ptr = NULL;

    if(!list_empty(&ft->free_list)){
        struct list_elem * e = list_pop_front(&ft->free_list);
        frame_ptr = list_entry(e, struct frame, elem);
        frame_ptr->thread = page_thread;
        frame_ptr->page = page;
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, e);
        return frame_ptr;
    }

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

    if(ft->clock_elem == NULL){
        ft->clock_elem = list_begin(&ft->used_list);
    }

    struct frame *victim = NULL;
    struct frame *curr = list_entry(ft->clock_elem, struct frame, elem);
    ASSERT(curr != NULL);
    struct page * victim_page_ptr = NULL;

    struct mapped_file_table *victim_mapped_file_table = NULL;
    uint32_t * victim_pd = NULL;

    while (victim == NULL) {
        ASSERT(curr->page != NULL && curr->page->page_location == PAGED_IN && curr->thread->pagedir != NULL); // used list attributes
        
        if (curr->pinned == true) {
            curr = get_next_frame(curr);
            continue;
        }



        // check if the frame has been accessed
        bool accessed = pagedir_is_accessed(curr->thread->pagedir, curr->page->uaddr);

        if (!accessed) {
            victim = curr;
            victim->pinned = true;
            victim_mapped_file_table = victim->thread->mapped_file_table;
            victim_pd = victim->thread->pagedir;
            victim_page_ptr = victim->page;
            victim->page->page_location = IN_TRANSIT;

            ASSERT(victim_pd != NULL);
            ASSERT(pagedir_get_page(victim_pd, victim->page->uaddr) == victim->kaddr);
            ASSERT(hash_find(&victim->thread->supp_pt->hash_map, &victim->page->hash_elem) != NULL);

            pagedir_clear_page(victim_pd, pg_round_down(victim->page->uaddr));

            ft->clock_elem = &get_next_frame(victim)->elem;
            list_remove(&victim->elem);

            break;
        } else {
            ASSERT(curr->page->uaddr != NULL);
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

        case MUNMAP:
        case MMAP:

            off_t read_bytes = victim->page->read_bytes;
            off_t ofs = victim->page->ofs;
            void * uaddr = victim->page->uaddr;
            mapid_t map_id = victim->page->map_id;
            
            victim->page = NULL;

            if (pagedir_is_dirty(victim_pd, uaddr)/* || pagedir_is_dirty(victim_pd, victim->kaddr)*/) {
                
                struct mapped_file * mapped_file = find_mapped_file(victim_mapped_file_table, map_id);
                ASSERT(mapped_file != NULL);


                
                lock_release(&vm_lock);
                lock_acquire(&fs_lock);
                
                file_write_at(mapped_file->file, victim->kaddr, read_bytes, ofs);
                
                lock_release(&fs_lock);
                lock_acquire(&vm_lock);
            }

            // if(victim->page != NULL){
            //     printf("location if not transit %d\n", victim->page->page_location);
            //     printf("victim uaddr %p\n", victim->page->uaddr);
            //     printf("frame victim was supposed to be eviction from %p\n", victim->kaddr);
            // }

            // if(victim_page_ptr->page_location != IN_TRANSIT){
            //     printf("location if not transit %d\n", victim_page_ptr->page_location);
            //     printf("victim uaddr %p\n", victim_page_ptr->uaddr);
            //     printf("frame victim was supposed to be eviction from %p\n", victim->kaddr);
            // }

            victim_page_ptr->page_location = PAGED_OUT;

            break;
        default:
            victim->page->page_location = PAGED_OUT;
            break;
    }
    cond_broadcast(&victim_page_ptr->transit, &vm_lock);

    victim->thread = NULL;
    victim->page = NULL;

    // frame fields set after return
    return victim;
}

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

struct frame * get_page_frame(struct page * page){
    for (struct list_elem *e = list_begin(&ft->used_list); e != list_end(&ft->used_list); e = list_next(e)){
        struct frame *frame = list_entry(e, struct frame, elem);

        ASSERT(frame->page != NULL);
        // ASSERT(frame->page->page_location == PAGED_IN);

        if(frame->page == page){
            return frame;
        }
    }
    return NULL;
}
