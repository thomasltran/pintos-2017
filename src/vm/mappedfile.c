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
    id = 0;
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
    mapped_file->map_id = id++;
    //printf("id added %d\n", mapped_file->map_id);

    return mapped_file;
}

// assumes vm lock already held
void munmap(mapid_t mapping){
    struct thread * cur = thread_current();

    struct mapped_file *mapped_file = NULL;
    for (struct list_elem *e = list_begin(&cur->mapped_file_table->list); e != list_end(&cur->mapped_file_table->list); e = list_next(e))
    {
        mapped_file = list_entry(e, struct mapped_file, elem);
        if (mapped_file->map_id == mapping)
        {
            break;
        }
        mapped_file = NULL;
    }

    if (mapped_file == NULL)
    {
        return;
    }

    int pages = (mapped_file->length + PGSIZE - 1) / PGSIZE; // round up formula
    void *curr = mapped_file->addr;

    struct supp_pt *supp_pt = cur->supp_pt;

    // we want to pin before
    // we want to check both uaddr and kaddr dirty
    
    for (int i = 0; i < pages; i++)
    {
        struct page *page = find_page(supp_pt, curr); // continguous
        ASSERT(page != NULL);
        if (!pagedir_is_dirty(cur->pagedir, curr))
        {
            hash_delete(&supp_pt->hash_map, &page->hash_elem);
            free(page);
            curr += PGSIZE;
            continue;
        }

        // for (int i = 0; i < pages; i++) {
        //     if (page != NULL) {
        //         char *byte = (char *)curr;
        //         for (int j = 0; j < 64 && j < page->read_bytes; j++) {
        //             printf("%02x ", byte[j]);
        //             if(j % 16 == 0 && j != 0){
        //                 printf("\n");
        //             }
        //         }
        //         printf("\n");
        //     }
        // }

        lock_release(&vm_lock);
        lock_acquire(&fs_lock);
        off_t wrote = file_write_at(mapped_file->file, page->uaddr, page->read_bytes, page->ofs);
        ASSERT((uint32_t)wrote == page->read_bytes);
        lock_release(&fs_lock);
        lock_acquire(&vm_lock);
        // ASSERT(wrote == page->read_bytes);
        curr += PGSIZE;

        hash_delete(&supp_pt->hash_map, &page->hash_elem);
        free(page);
    }
    //list_remove(&mapped_file->elem);
}