#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include <stdbool.h>

#define CACHE_SIZE 64 // limit cache size to 64 sectors (blocks)

/* Type definitions for cache block & readahead block (defined in cache.c) */
struct cache_block;

/* Initializes the cache system. */
void cache_init(void);

/* Flushes all dirty blocks in cache to disk. Used for synchronization and eviction. */
void cache_flush(void);

/* Gets a block from the cache for the given sector.
 * If exclusive is true, gets exclusive write access. */
struct cache_block *cache_get_block(block_sector_t sector, bool exclusive);

/* Releases a previously acquired cache block. */
void cache_put_block(struct cache_block *block);

/* Reads data from a cache block. Requires prior cache_get_block call. */
void *cache_read_block(struct cache_block *block);

/* Zeros out a cache block. Used when allocating new blocks. */
void *cache_zero_block(struct cache_block *block);

/* Marks a cache block as dirty, indicating it needs to be written back to disk. */
void cache_mark_dirty(struct cache_block *block);

#endif