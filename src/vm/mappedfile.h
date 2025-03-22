#ifndef VM_MAPPEDFILE_H
#define VM_MAPPEDFILE_H

#include <lib/kernel/list.h>
#include <threads/synch.h>
#include "filesys/file.h"
#include <stdio.h>

typedef int mapid_t;

// table to track which files are mapped into which pages
struct mapped_file_table {
    struct list list; // list of mapped files
};

// mapped file
struct mapped_file {
    void * addr; // address passed in
    struct file * file; // mapped file
    size_t length; // file length
    struct list_elem elem; // elem for list
    mapid_t map_id; // map id, increases
};


struct mapped_file_table *create_mapped_file_table(void);
struct mapped_file * create_mapped_file(struct file * file, void * addr, off_t length);
void free_mapped_file_table(struct mapped_file_table * mapped_file_table);
mapid_t * mmap (int fd, void *addr);
bool free_mapped_file (mapid_t mapping, struct mapped_file_table * mapped_file_table);
struct mapped_file *find_mapped_file(struct mapped_file_table *mapped_file_table, mapid_t map_id);
extern mapid_t id; // map_id is set to this

#endif