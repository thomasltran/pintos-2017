#include "vm/mappedfile.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

mapid_t id;

struct mapped_file_table *create_mapped_file_table()
{
    struct mapped_file_table * mapped_file_table = malloc(sizeof(struct mapped_file_table));
    if (mapped_file_table == NULL)
    {
        return NULL;
    }
    list_init(&mapped_file_table->list);
    id = -1;
    return mapped_file_table;
}

struct mapped_file * create_mapped_file(struct file * file, void * addr, off_t length)
{
    struct mapped_file * mapped_file = malloc(sizeof(struct mapped_file));
    if(mapped_file == NULL){
        return NULL;
    }
    mapped_file->file = file; // call reopen before, use that ref
    mapped_file->addr = addr;
    mapped_file->length = length;
    mapped_file->map_id = ++id;

    return mapped_file;
}

void free_mapped_file_table(struct mapped_file_table * mapped_file_table){  
    for (struct list_elem *e = list_begin(&mapped_file_table->list); e != list_end(&mapped_file_table->list);){
      struct mapped_file * mapped_file = list_entry(e, struct mapped_file, elem);
      free_mapped_file(mapped_file->map_id, mapped_file_table);
      e = list_remove(&mapped_file->elem);
      free(mapped_file);
    }
    free(mapped_file_table);
}


// assumes vm lock already held
void free_mapped_file(mapid_t mapping, struct mapped_file_table * mapped_file_table){
    struct thread * cur = thread_current();

    struct mapped_file *mapped_file = find_mapped_file(mapped_file_table, mapping);

    ASSERT(mapped_file != NULL);

    if (!get_pinned_frames(mapped_file->addr, true, mapped_file->length))
    {
      ASSERT(1 == 2); // fail
    }

    int pages = (mapped_file->length + PGSIZE - 1) / PGSIZE; // round up formula
    void *curr = mapped_file->addr;

    struct supp_pt *supp_pt = cur->supp_pt;
    
    for (int i = 0; i < pages; i++)
    {
        struct page *page = find_page(supp_pt, curr); // continguous
        ASSERT(page != NULL);
        ASSERT(page->page_location == PAGED_IN);
        ASSERT(page->page_status == MMAP);

        struct frame * frame = get_page_frame(page);
        ASSERT(frame != NULL);
        
        if (pagedir_is_dirty(cur->pagedir, page->uaddr)/* && pagedir_is_dirty(cur->pagedir, frame->kaddr)*/)
        {
            lock_release(&vm_lock);
            lock_acquire(&fs_lock);

            off_t wrote = file_write_at(mapped_file->file, frame->kaddr, page->read_bytes, page->ofs);

            lock_release(&fs_lock);
            lock_acquire(&vm_lock);
        }

        page->page_status = MUNMAP; // used in page fault
        page_frame_freed(frame); // will unpin
        curr += PGSIZE;

        continue;
    }
}

struct mapped_file *find_mapped_file(struct mapped_file_table *mapped_file_table, mapid_t map_id)
{
    struct mapped_file *mapped_file = NULL;

    for (struct list_elem *e = list_begin(&mapped_file_table->list); e != list_end(&mapped_file_table->list); e = list_next(e))
    {
        mapped_file = list_entry(e, struct mapped_file, elem);

        if (mapped_file->map_id == map_id)
        {
            break;
        }
        mapped_file = NULL;
    }
    return mapped_file;
}