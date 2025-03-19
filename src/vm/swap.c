#include "lib/kernel/bitmap.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "lib/round.h"
#include "lib/debug.h"
#include "threads/malloc.h"
#include <stdio.h>
#include "swap.h"
// // tracks usage of swap slots.
struct swap_table {
    struct block * swap_block;
    struct bitmap * bitmap; // block_size(swap_block) * BLOCK_SECTOR_SIZE / PAGE_SIZE
    struct lock lock;
};


static struct swap_table* st;


void init_st(){
    st = malloc(sizeof(struct swap_table));
    st->swap_block = block_get_role(BLOCK_SWAP);
    if(st->swap_block == NULL){
        ASSERT(1 == 2);
        return;
    }
    st->bitmap = bitmap_create(block_size(st->swap_block) * BLOCK_SECTOR_SIZE/PGSIZE);

    ASSERT(bitmap_all(st->bitmap, 0, block_size(st->swap_block) * BLOCK_SECTOR_SIZE/PGSIZE) == false);
    
    // Uncomment if we break up vm_lock into smaller locks for efficiency
    // lock_init(&st->lock);
}

// evict out to
size_t st_write_at(void* uaddr){
    size_t map_id = bitmap_scan_and_flip(st->bitmap, 0, 1, false);
    ASSERT(map_id != BITMAP_ERROR);

    ASSERT(bitmap_test(st->bitmap, map_id) == true);
    size_t sector_in_page = PGSIZE / BLOCK_SECTOR_SIZE;

    for(size_t i = 0; i < sector_in_page; i++){
        block_write(st->swap_block, (map_id * PGSIZE/BLOCK_SECTOR_SIZE)+i, uaddr + (i * BLOCK_SECTOR_SIZE));

    }
    return map_id;
}


void st_free_page(size_t id){
    bitmap_reset(st->bitmap,id);
}

// bring into
void st_read_at(void* uaddr, size_t id){
    ASSERT(bitmap_test(st->bitmap,id) == true);
    size_t sector_in_page = PGSIZE / BLOCK_SECTOR_SIZE;

    for(size_t i = 0; i < sector_in_page; i++){
        block_read(st->swap_block, (id * PGSIZE/BLOCK_SECTOR_SIZE) + i, uaddr+ (i *BLOCK_SECTOR_SIZE));
    }

    st_free_page(id);  
}
