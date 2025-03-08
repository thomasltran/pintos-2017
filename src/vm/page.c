#include <vm/page.h>
#include <threads/synch.h>


// code or data/bss pages in it are virtually allocated (which
//     will happen in load_segment), or as stack pages are added or mmap mappings are
//     created.
// enables page fault handling by supplementing the page table
struct supp_pt {
    struct hash hash_map;
    // uaddr key, value page
    struct lock lock;
};

struct page {
    void * uaddr;
    enum page_status;
    struct lock lock;
    struct hash_elem;

    struct file * file; // if page refers to a mapped file
    size_t swap_index; // if page in swap space
};

enum page_status {
    PHYS, // frame table
    SWAP, // swap space
    MAPPED, // mapped to file
    CODE, // ucode
    DATA, // data
    BSS, // bss
    STACK // ustack
};