#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "vm/mappedfile.h"
#include "vm/frame.h"

extern struct lock vm_lock;

enum page_status {
    MMAP, // mapped to file
    MUNMAP, // unmapped
    CODE, // ucode
    DATA_BSS, // data or bss
    STACK, // ustack
    S_UNKNOWN //status unknown
};


enum page_location {
    PAGED_IN,
    PAGED_OUT,
    SWAP
};

// code or data/bss pages in it are virtually allocated (which
//     will happen in load_segment), or as stack pages are added or mmap mappings are
//     created.
// enables page fault handling by supplementing the page table
struct supp_pt {
    struct hash hash_map;
    // uaddr key, value page
    // struct lock lock;
};

struct page {
    void * uaddr;
    enum page_status page_status;
    enum page_location page_location;
    // struct lock lock;
    struct hash_elem hash_elem;

    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;

    struct file * file; // segment in file
    size_t swap_index; // if page in swap space
    mapid_t map_id;
};

// lock init
void init_spt(void);

// created at thread startup
struct supp_pt * create_supp_pt(void);

struct page * create_page(void * uaddr, struct file * file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_status, enum page_location);

struct page *find_page(struct supp_pt *supp_pt, void *uaddr);

void free_spt(struct supp_pt *supp_pt);

#endif /* threads/thread.h */

