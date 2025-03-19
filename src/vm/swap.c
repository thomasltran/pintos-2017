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


struct swap_table* st;


void init_st(){
    st = malloc(sizeof(struct swap_table));
    st->swap_block = block_get_role(BLOCK_SWAP);
    st->bitmap = bitmap_create(block_size(st->swap_block) * BLOCK_SECTOR_SIZE/PGSIZE);
    
    // Uncomment if we break up vm_lock into smaller locks for efficiency
    // lock_init(&st->lock);
}

size_t st_write_at(void* uaddr, uint32_t write_bytes){
    size_t page_cnt = DIV_ROUND_UP(write_bytes, PGSIZE);
    size_t map_id = bitmap_scan_and_flip(st->bitmap, 0, page_cnt, false);
    ASSERT(map_id != BITMAP_ERROR);
    if(write_bytes==0){
        bitmap_mark(st->bitmap, map_id);
    }

    // printf("map id: %zu\n", map_id);
    // printf("%d\n", bitmap_test(st->bitmap, map_id));
    ASSERT(bitmap_test(st->bitmap, map_id) == true);
    size_t sector_cnt = DIV_ROUND_UP(write_bytes, BLOCK_SECTOR_SIZE);

    for(size_t i = 0; i < sector_cnt; i++){
        block_write(st->swap_block, (map_id * PGSIZE/BLOCK_SECTOR_SIZE)+i, uaddr + (i * BLOCK_SECTOR_SIZE));

    }
    return map_id;
}


void st_free_page(size_t id){
    bitmap_reset(st->bitmap,id);
}

void st_read_at(void* uaddr, uint32_t read_bytes, size_t id, bool free){
    ASSERT(bitmap_test(st->bitmap,id) == true);
    size_t sector_cnt = DIV_ROUND_UP(read_bytes, BLOCK_SECTOR_SIZE);

    for(size_t i = 0; i < sector_cnt; i++){
        block_read(st->swap_block, (id * PGSIZE/BLOCK_SECTOR_SIZE) + i, uaddr+ (i *BLOCK_SECTOR_SIZE));


    }

    if(free){
        st_free_page(id);

        static char zeros[BLOCK_SECTOR_SIZE];
        size_t i;
        
        for (i = 0; i < sector_cnt; i++){
            block_write (st->swap_block, id + i, zeros);
        }

    }    

}
