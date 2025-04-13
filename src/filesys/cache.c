#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include <string.h>
#include "list.h"
#include "devices/timer.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"
#include <stdio.h>

// struct cache block
struct cache_block {
    bool valid; // data is up to date
    bool dirty; // more recent data than on disk
    bool exclusive; // writer
    block_sector_t sector; // sector
    uint8_t data[BLOCK_SECTOR_SIZE]; // data
    struct lock lock; // lock for cache block
    struct condition read_cond; // readers
    struct condition write_cond; // writers
    int readers; // num of readers
    struct list_elem lru_elem; // lru list
};

// holds info for readahead
struct readahead_entry
{
    block_sector_t sector; // sector
    struct list_elem elem; // elem for readahead list
};

static struct cache_block *find_block(block_sector_t sector);
static struct lock buffer_cache_lock; // lock for global buff cache array
static struct lock lru_lock; // lock for lru list

// global buff cache array
static struct cache_block cache[CACHE_SIZE];

// lru list
static struct list lru_list;

static struct list readahead_queue; // readahead queue
static struct lock readahead_lock; // readahead lock
static struct condition cb_available; // task available, or shutdown
static bool shutdown; // if filesys done is called

static void cache_readahead_daemon(void *aux UNUSED);

// adds the sector to the queue
void cache_readahead(block_sector_t sector)
{
    lock_acquire(&readahead_lock);

    if (shutdown)
    {
        cond_signal(&cb_available, &readahead_lock);
        lock_release(&readahead_lock);
        return;
    }

    ASSERT(sector != UINT32_MAX && sector != UINT32_MAX - 1);

    struct readahead_entry *readahead_entry = malloc(sizeof(struct readahead_entry));
    readahead_entry->sector = sector;

    list_push_back(&readahead_queue, &readahead_entry->elem);
    cond_signal(&cb_available, &readahead_lock);

    lock_release(&readahead_lock);
}

// prefetch the block (sector), reading will make the cache block valid
// could get evicted, but just trying to increase perf
static void cache_readahead_daemon()
{
    lock_acquire(&readahead_lock);

    while (!shutdown)
    {
        if (!list_empty(&readahead_queue))
        {
            ASSERT(!shutdown);

            struct list_elem *e = list_pop_front(&readahead_queue);
            struct readahead_entry *readahead_entry = list_entry(e, struct readahead_entry, elem);

            ASSERT(readahead_entry != NULL);
            ASSERT(readahead_entry->sector != UINT32_MAX && readahead_entry->sector != UINT32_MAX - 1);

            cache_get_block(readahead_entry->sector, false, true);
            free(readahead_entry);
        }

        if (shutdown)
        {
            lock_release(&readahead_lock);
            thread_exit();
        }
        cond_wait(&cb_available, &readahead_lock);
    }

    lock_release(&readahead_lock);
    thread_exit();
}

// writes back dirty blocks periodically
static void flush_daemon(void *aux UNUSED)
{
    while (true)
    {
        timer_sleep(30 * TIMER_FREQ);
        cache_flush(false);
    }
}

// init
void cache_init(void)
{
    list_init(&lru_list);
    lock_init(&buffer_cache_lock);
    lock_init(&lru_lock);

    list_init(&readahead_queue);
    lock_init(&readahead_lock);
    cond_init(&cb_available);

    shutdown = false;

    for (size_t i = 0; i < CACHE_SIZE; i++)
    {
        struct cache_block *cb = &cache[i];

        cb->valid = false;
        cb->dirty = false;
        cb->exclusive = false;
        cb->sector = UINT32_MAX;
        cb->readers = 0;
        lock_init(&cb->lock);
        cond_init(&cb->read_cond);
        cond_init(&cb->write_cond);
        list_push_front(&lru_list, &cb->lru_elem);
    }

    thread_create("flush-daemon", -20, flush_daemon, NULL);
    thread_create("cache_readahead_daemon", -20, cache_readahead_daemon, NULL);
}

// flush dirty cache blocks
void cache_flush(bool fs_done)
{
    if (fs_done)
    {
        lock_acquire(&readahead_lock);

        for (struct list_elem *e = list_begin(&readahead_queue);
             e != list_end(&readahead_queue);)
        {
            struct readahead_entry *readahead_entry = list_entry(e, struct readahead_entry, elem);
            e = list_remove(e);
            free(readahead_entry);
        }

        shutdown = true;
        cond_signal(&cb_available, &readahead_lock);

        lock_release(&readahead_lock);
    }

    lock_acquire(&buffer_cache_lock);
    for (size_t i = 0; i < CACHE_SIZE; i++)
    {
        struct cache_block *cb = &cache[i];
        lock_acquire(&cb->lock);
        if (cb->dirty)
        {
            block_write(fs_device, cb->sector, cb->data);
            cb->dirty = false;
        }
        lock_release(&cb->lock);
    }
    lock_release(&buffer_cache_lock);
}

// gets a cache block
struct cache_block *cache_get_block(block_sector_t sector, bool exclusive, bool readahead)
{
    ASSERT(sector != UINT32_MAX && sector != UINT32_MAX - 1);
    struct cache_block *cb = find_block(sector);
    // cb lock still held after find_block

    // exclusive
    if (exclusive)
    {
        while (cb->readers > 0 || cb->exclusive)
        {
            cond_wait(&cb->write_cond, &cb->lock);
        }
        cb->exclusive = true;
    }
    // shared
    else
    {
        while (cb->exclusive)
        {
            cond_wait(&cb->read_cond, &cb->lock);
        }
        cb->readers++;
    }

    lock_release(&cb->lock);

    if (readahead)
    {
        cache_read_block(cb);
        cache_put_block(cb);
        cb = NULL;
    }

    return cb;
}

// retrieves a block for the given sector
static struct cache_block *find_block(block_sector_t sector)
{
    struct cache_block *cb = NULL;

    lock_acquire(&buffer_cache_lock);

    // already in buff cache
    for (size_t i = 0; i < CACHE_SIZE; i++)
    {
        cb = &cache[i];
        lock_acquire(&cb->lock);
        if (cb->sector == sector)
        {
            lock_release(&buffer_cache_lock);
            return cb;
        }
        lock_release(&cb->lock);
    }
    lock_release(&buffer_cache_lock);

    // evict
    while (true)
    {
        lock_acquire(&lru_lock);

        for (struct list_elem *e = list_begin(&lru_list);
             e != list_end(&lru_list);)
        {
            cb = list_entry(e, struct cache_block, lru_elem);
            lock_acquire(&cb->lock);

            if (!cb->exclusive && cb->readers == 0)
            {
                lock_release(&lru_lock);

                if (cb->dirty)
                {
                    block_write(fs_device, cb->sector, cb->data);
                }

                cb->sector = sector;
                cb->valid = false;
                cb->dirty = false;

                return cb;
            }
            e = list_next(e);
            lock_release(&cb->lock);
        }
        lock_release(&lru_lock);
        timer_sleep(100); // find better time
    }

    return NULL;
}

// put block
void cache_put_block(struct cache_block *block)
{

    lock_acquire(&lru_lock);
    lock_acquire(&block->lock);
    // block was exclusive
    if (block->exclusive)
    {
        block->exclusive = false;
        cond_broadcast(&block->read_cond, &block->lock);
        cond_signal(&block->write_cond, &block->lock);
    }
    // shared
    else
    {
        block->readers--;
        if (block->readers == 0)
        {
            cond_signal(&block->write_cond, &block->lock);
        }
    }
    lock_release(&block->lock);

    if (!block->exclusive && block->readers == 0)
    {
        list_remove(&block->lru_elem);
        list_push_back(&lru_list, &block->lru_elem);
    }
    lock_release(&lru_lock);
}

// read into block's data
void *cache_read_block(struct cache_block *block)
{
    lock_acquire(&block->lock);
    if (!block->valid)
    {
        block_read(fs_device, block->sector, block->data);
        block->valid = true;
    }
    lock_release(&block->lock);
    return block->data;
}

// zero's block
void *cache_zero_block(struct cache_block *block)
{
    lock_acquire(&block->lock);
    if (!block->valid)
    {
        memset(block->data, 0, BLOCK_SECTOR_SIZE);
        block->valid = true;
    }
    lock_release(&block->lock);
    return block->data;
}

// mark dirty
void cache_mark_dirty(struct cache_block *block)
{
    lock_acquire(&block->lock);
    block->dirty = true;
    lock_release(&block->lock);
}
