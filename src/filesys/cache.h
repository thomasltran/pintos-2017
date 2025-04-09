#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"

#define CACHE_SIZE 64

// cache.h
struct cache_block;    		// opaque type

// reserve a block in buffer cache dedicated to hold this sector
// possibly evicting some other unused buffer
// either grant exclusive or shared access
struct cache_block * cache_get_block (block_sector_t sector, bool exclusive);

// release access to cache block
void cache_put_block(struct cache_block *b);

// read cache block from disk, returns pointer to data
void *cache_read_block(struct cache_block *b);

// fill cache block with zeros, returns pointer to data
void *cache_zero_block(struct cache_block *b);

// mark cache block dirty (must be written back)
void cache_mark_block_dirty(struct cache_block *b);

void cache_init(void);

// not shown: initialization, readahead, shutdown
#endif /* filesys/filesys.h */
