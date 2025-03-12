#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "filesys/file.h"

extern struct lock vm_lock;

enum page_status {
    PHYS, // frame table
    SWAP, // swap space
    MAPPED, // mapped to file
    CODE, // ucode
    DATA, // data
    BSS, // bss
    STACK, // ustack
    UNKNOWN
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
    // struct lock lock;
    struct hash_elem hash_elem;

    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;

    struct file * file; // segment in file
    size_t swap_index; // if page in swap space
};

// lock init
void init_spt(void);

// created at thread startup
struct supp_pt * create_supp_pt(void);

struct page * create_page(void * uaddr, struct file * file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_status);

struct page *find_page(struct supp_pt *supp_pt, void *uaddr);

#endif /* threads/thread.h */

