#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "vm/mappedfile.h"
#include "vm/frame.h"
#include "threads/synch.h"

// global vm lock
extern struct lock vm_lock;

// status of a page set on creation
enum page_status {
    MMAP, // mapped to file
    MUNMAP, // unmapped—if MUNMAP, shouldn't access the mapping anymore
    CODE, // ucode
    DATA_BSS, // data and/or bss
    STACK// ustack
};

// location of page in memory
enum page_location {
    PAGED_IN, // in a page frame
    PAGED_OUT, // not in a page frame
    SWAP, // in swap space
    IN_TRANSIT // for eviction
};

// enables page fault handling by supplementing the page table
struct supp_pt {
    struct hash hash_map; // uaddr key, value is struct page
};

// page entry in the spt
struct page {
    void * uaddr;
    enum page_status page_status;
    enum page_location page_location;
    struct hash_elem hash_elem;

    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;

    struct file * file; // exe file
    size_t swap_index; // if page in swap space
    mapid_t map_id; // if page is for a mapped file
    struct condition transit; // for eviction
};

void init_spt(void);
struct supp_pt * create_supp_pt(void);
void free_spt(struct supp_pt *supp_pt);

struct page * create_page(void * uaddr, struct file * file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_status, enum page_location);
struct page *find_page(struct supp_pt *supp_pt, void *uaddr);
bool install_page_in_frame(struct page *page, struct thread *thread_cur, bool stack_growth, bool write, bool pinned, bool page_fault);

#endif

