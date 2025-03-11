
#include <lib/kernel/list.h>
#include <lib/kernel/hash.h>
#include <threads/synch.h>

typedef int mapid_t;

// you need a table to track which files are mapped into which pages
struct file_mapping_table {
    struct hash hash_map;
    struct lock lock;
};

struct mapped_file {
    size_t file_length;
    void * addr; // address passed in
    struct list_elem elem;
    mapid_t map;
};
//void * mmap (int fd, void *addr);