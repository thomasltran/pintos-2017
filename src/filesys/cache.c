#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>
#include <stdbool.h>
#include "filesys/cache.h"

// cache.h
struct cache_block {
    block_sector_t sector_id;

    bool dirty;
    bool valid;
    bool exclusive; // for writers

    int num_readers;

    struct lock lock;
    struct condition readers_cond;
    struct condition writers_cond;
    
    struct list_elem elem; // lru eviction

    uint8_t data[BLOCK_SECTOR_SIZE];
};

static struct cache_block cache[CACHE_SIZE];
static struct list lru_list;

void cache_init(){
    list_init(&lru_list);

    for(int i = 0; i<CACHE_SIZE; i++){
        struct cache_block *cb = &cache[i];
        cb->sector_id = UINT32_MAX;
        cb->dirty = false;
        cb->valid = false;
        cb->exclusive = false;
        cb->num_readers = 0;
        lock_init(&cb->lock);
        cond_init(&cb->readers_cond);
        cond_init(&cb->writers_cond);
    }
}

static struct cache_block * find_cache_block(){
    struct cache_block *cb = NULL;
    for(int i = 0; i<CACHE_SIZE; i++){
        cb = &cache[i];
        lock_acquire(&cb->lock);
        if(cb->sector_id == UINT32_MAX){
            lock_release(&cb->lock);
            return cb;
        }
    }

    ASSERT(list_size(&lru_list) != 0);
    struct list_elem * e = list_pop_back(&lru_list);
    cb = list_entry(e, struct cache_block, elem);
    return cb;
}

// reserve a block in buffer cache dedicated to hold this sector
// possibly evicting some other unused buffer
// either grant exclusive or shared access
struct cache_block * cache_get_block (block_sector_t sector, bool exclusive){
    return NULL;
}

// release access to cache block
void cache_put_block(struct cache_block *b){

}

// read cache block from disk, returns pointer to data
void *cache_read_block(struct cache_block *b){
    return NULL;

}

// fill cache block with zeros, returns pointer to data
void *cache_zero_block(struct cache_block *b){
    return NULL;

}

// mark cache block dirty (must be written back)
void cache_mark_block_dirty(struct cache_block *b){

}

// not shown: initialization, readahead, shutdown
