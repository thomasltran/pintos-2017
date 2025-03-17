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

//frame table
//static because frame.c should use it directly while other compilation units should utilzie frame table functions
static struct frame_table* ft;


void init_ft(){
    ft = malloc(sizeof(struct frame_table));
    list_init(&(ft->free_list));
    list_init(&(ft->used_list));

    uint32_t* kpage;

    while((kpage = palloc_get_page(PAL_USER))){
        struct frame * frame_ptr = malloc(sizeof(struct frame));
        frame_ptr->kaddr = kpage;
        list_push_front(&ft->free_list, &frame_ptr->elem);
    }



}

void* ft_get_page(struct thread* page_thread, void* u_vaddr, bool pinned){
    //primitive implementation
    if(!list_empty(&ft->free_list)){
        struct list_elem * e = list_pop_front(&ft->free_list);
        struct frame * frame_ptr = list_entry(e, struct frame, elem);
        frame_ptr->thread = page_thread;
        frame_ptr->page = find_page(page_thread->supp_pt, u_vaddr);
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, e);
        return frame_ptr->kaddr;
    }
    else{
        // return list_entry(list_pop_front(&ft->used_list), struct frame, elem)->kaddr;
        struct list_elem * e = list_pop_front(&ft->used_list);
        struct frame * frame_ptr = list_entry(e, struct frame, elem);

        pagedir_clear_page(frame_ptr->thread->pagedir, pg_round_down(frame_ptr->page->uaddr));
        pagedir_set_accessed(frame_ptr->thread->pagedir, pg_round_down(frame_ptr->page->uaddr), false);
        pagedir_set_dirty(frame_ptr->thread->pagedir, pg_round_down(frame_ptr->page->uaddr), false);
        
        frame_ptr->thread = page_thread;
        frame_ptr->page = find_page(page_thread->supp_pt, u_vaddr);
        frame_ptr->pinned = pinned;
        list_push_front(&ft->used_list, e);
        return frame_ptr->kaddr;
    }

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