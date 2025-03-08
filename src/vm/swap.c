#include <lib/kernel/bitmap.h>
#include <threads/synch.h>

// tracks usage of swap slots.
static struct swap_table {
    struct block * swap_block;
    struct bitmap * bitmap; // block_size(swap_block) * BLOCK_SECTOR_SIZE / PAGE_SIZE
    struct lock lock;
};