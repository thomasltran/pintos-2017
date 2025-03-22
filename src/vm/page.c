#include "page.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "userprog/exception.h"
#include <stdio.h>
#include "userprog/syscall.h"
#include "lib/string.h"

struct lock vm_lock; // global vm lock

// hash table functions
static unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED);
static bool
page_less(const struct hash_elem *a_, const struct hash_elem *b_,
          void *aux UNUSED);

static void free_page(struct hash_elem *e, void *aux UNUSED);

// init vm lock
void init_spt()
{
    lock_init(&vm_lock);
}

// init spt
struct supp_pt *create_supp_pt(void)
{
    struct supp_pt *supp_pt = malloc(sizeof(struct supp_pt));
    if (supp_pt == NULL)
    {
        return NULL;
    }
    if(hash_init(&supp_pt->hash_map, page_hash, page_less, NULL) == false){
        free_spt(supp_pt);
        return NULL;
    }
    return supp_pt;
}

// creates a page entry for spt
struct page *create_page(void *uaddr, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_status page_status, enum page_location page_location)
{
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL)
    {
        return NULL;
    }
    page->uaddr = uaddr;
    page->file = file;
    page->ofs = ofs;
    page->read_bytes = read_bytes;
    page->zero_bytes = zero_bytes;
    page->writable = writable;
    page->page_status = page_status;
    page->page_location = page_location;
    page->map_id = -1;
    page->swap_index = UINT32_MAX;
    cond_init(&page->transit);

    return page;
}

// finds struct page in spt hash table
struct page *find_page(struct supp_pt *supp_pt, void *uaddr)
{
    struct hash hash_map = supp_pt->hash_map;
    struct page page;
    page.uaddr = uaddr;

    struct hash_elem *e = hash_find(&hash_map, &page.hash_elem);
    if(e == NULL){
        return NULL;
    }
    return hash_entry(e, struct page, hash_elem);
}

/* Returns a hash value for page p. */
static unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    void * round = pg_round_down(p->uaddr);
    return hash_bytes(&round, sizeof(round));
}

/* Returns true if page a precedes page b. */
static bool
page_less(const struct hash_elem *a_, const struct hash_elem *b_,
          void *aux UNUSED)
{
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    return pg_round_down(a->uaddr) < pg_round_down(b->uaddr);
}

// ps exit, frees swap slot if applicable, page out any page frames the struct page may still occupy
void free_spt(struct supp_pt *supp_pt){
    hash_destroy(&supp_pt->hash_map, free_page); // or clear?
    free(supp_pt);
}

// action func for hash_destroy
static void free_page(struct hash_elem *e, void *aux UNUSED){
    // mapped files wb and freed before in ps exit
    struct page *page = hash_entry(e, struct page, hash_elem);

    //
    if(page->swap_index != UINT32_MAX){
        st_free_page(page->swap_index);
    }

    if(page->page_location == PAGED_IN){
        struct frame * frame = get_page_frame(page);
        ASSERT(frame != NULL);

        page_frame_freed(frame);
    }

    free(page);
}

// install a struct page in a page frame, for get_pinned_frames and page_fault
//
bool install_page_in_frame(struct page *page, struct thread *thread_cur, bool stack_growth, bool write, bool pinned, bool page_fault)
{
    // move 78 https://www.youtube.com/watch?v=mzZWPcgcRD0
    while (page->page_location == IN_TRANSIT)
    {
        cond_wait(&page->transit, &vm_lock);
    }

    // if munmap'ed or ps exit (implicitly closed)
    if (page_fault && page->page_status == MUNMAP)
    {
        return false;
    }
    // fine if not a page fault, just reset back
    else if (!page_fault && page->page_status == MUNMAP)
    {
        page->page_status = MMAP;
    }

    // can't write to non-writable segment (e.g. code)
    if (write && !page->writable)
    {
        return false;
    }

    // final check, shouldn't hit here but just in case
    struct frame *temp_frame = get_page_frame(page);
    if (temp_frame != NULL)
    {
        ASSERT(page->page_location != PAGED_IN);
        page_frame_freed(temp_frame);
    }

    ASSERT(pagedir_get_page(thread_cur->pagedir, pg_round_down(page->uaddr)) == NULL);

    void *upage = pg_round_down(page->uaddr);

    struct frame *frame = ft_get_page_frame(thread_cur, page, true);

    uint8_t *kpage = frame->kaddr;

    ASSERT(kpage != NULL);

    // if page in swap, make sure to page in
    bool in_swap = page->page_location == SWAP;
    if (in_swap)
    {
        ASSERT(page->swap_index != UINT32_MAX);
    }

    page->page_location = PAGED_IN;

    if (pagedir_get_page(thread_cur->pagedir, upage) != NULL || pagedir_set_page(thread_cur->pagedir, upage, kpage, page->writable) == false)
    {
        page_frame_freed(frame);
        return false;
    }

    // fetch data into frame
    if (!stack_growth)
    {
        if ((page->page_status == DATA_BSS || page->page_status == STACK) && in_swap)
        {
            st_read_at(kpage, page->swap_index);
            page->swap_index = UINT32_MAX;
        }
        else
        {
            lock_release(&vm_lock);
            lock_acquire(&fs_lock);
            file_seek(page->file, page->ofs);
            if (file_read(page->file, kpage, page->read_bytes) != (int)page->read_bytes)
            {
                return false;
            }
            lock_release(&fs_lock);
            lock_acquire(&vm_lock);
        }
    }
    // don't zero out the paged in data from swap just acquired
    if (!in_swap)
    {
        memset(kpage + page->read_bytes, 0, page->zero_bytes);
    }

    // should be true at this point
    ASSERT(page->page_location == PAGED_IN);
    ASSERT(frame->pinned == true);
    ASSERT(frame->thread == thread_cur);

    // unpin if not requested
    if (!pinned)
    {
        frame->pinned = false;
    }
    return true;
}