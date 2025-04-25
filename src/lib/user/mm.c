/*
 * Simple, 64-bit allocator based on implicit free lists,
 * first fit placement, and boundary tag coalescing, as described
 * in the CS:APP2e text. Blocks must be aligned to 16 byte
 * boundaries. Minimum block size is 16 bytes.
 *
 * This version is loosely based on
 * http://csapp.cs.cmu.edu/3e/ics3/code/vm/malloc/mm.c
 * but unlike the book's version, it does not use C preprocessor
 * macros or explicit bit operations.
 *
 * It follows the book in counting in units of 4-byte words,
 * but note that this is a choice (my actual solution chooses
 * to count everything in bytes instead.)
 *
 * You should use this code as a starting point for your
 * implementation.
 *
 * First adapted for CS3214 Summer 2020 by gback
 */
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include "lib/kernel/list.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mm.h"
#include "lib/debug.h"

#define ALIGNMENT 16

static char * mem_brk = NULL;
static char * mem_max_addr = NULL;

static void * mem_sbrk(int incr) {
    char * old_brk = mem_brk;
    
    if ((mem_brk + incr) > mem_max_addr)
        return NULL;
    
    mem_brk += incr;
    return (void *)old_brk;
}

struct boundary_tag {
  int inuse : 1; // inuse bit
  int size : 31; // size of block, in words
                 // block size
};

/* FENCE is used for heap prologue/epilogue. */
const struct boundary_tag FENCE = {.inuse = -1, .size = 0};

/* A C struct describing the beginning of each block.
 * For implicit lists, used and free blocks have the same
 * structure, so one struct will suffice for this example.
 *
 * If each block is aligned at 12 mod 16, each payload will
 * be aligned at 0 mod 16.
 */
struct block {
  struct boundary_tag header; /* offset 0, at address 12 mod 16 */
  union {
    char payload[0]; /* offset 4, at address 0 mod 16 */
    struct list_elem elem;
  };
} __attribute__((packed));

/* Basic constants and macros */
#define WSIZE                                                                  \
  sizeof(struct boundary_tag)  /* Word and header/footer size (bytes) */
#define MIN_BLOCK_SIZE_WORDS 8 /* Minimum block size in words */
#define CHUNKSIZE (1 << 6)     /* Extend heap by this amount (words) */
#define NUM_LISTS 10           // number of segregated lists
#define SL0 8                  // minimum size of blocks in seg_list[0]
#define SL1 32                 // minimum size of blocks in seg_list[1]
#define SL2 64
#define SL3 128
#define SL4 256
#define SL5 512
#define SL6 1024
#define SL7 2048
#define SL8 4096
#define SL9 32768

static inline size_t max(size_t x, size_t y) { return x > y ? x : y; }

static size_t align(size_t size) {
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static bool is_aligned(size_t size) __attribute__((__unused__));
static bool is_aligned(size_t size) { return size % ALIGNMENT == 0; }

/* Global variables */
static struct block *heap_listp = 0; /* Pointer to first block */
static struct list seg_lists[NUM_LISTS];
static struct block *find_fit_helper(int sl_index, size_t asize);

/* Function prototypes for internal helper routines */
static struct block *extend_heap(size_t words);
static void place(struct block *bp, size_t asize);
static struct block *find_fit(size_t asize);
static struct block *coalesce(struct block *bp);
// static int mm_checkheap();

/* Given a block, obtain previous's block footer.
   Works for left-most block also. */
static struct boundary_tag *prev_blk_footer(struct block *blk) {
  // return &blk->header -1;
  return ((void *)blk - sizeof(struct boundary_tag));
}

/* Return if block is free */
static bool blk_free(struct block *blk) { return !blk->header.inuse; }

/* Return size of block is free */
static size_t blk_size(struct block *blk) { return blk->header.size; }

/* Given a block, obtain pointer to previous block.
   Not meaningful for left-most block. */
static struct block *prev_blk(struct block *blk) {
  struct boundary_tag *prevfooter = prev_blk_footer(blk);
  ASSERT(prevfooter->size != 0);
  return (struct block *)((void *)blk - WSIZE * prevfooter->size);
}

/* Given a block, obtain pointer to next block.
   Not meaningful for right-most block. */
static struct block *next_blk(struct block *blk) {
  ASSERT(blk_size(blk) != 0);
  return (struct block *)((void *)blk + WSIZE * blk->header.size);
}

/* Given a block, obtain its footer boundary tag */
static struct boundary_tag *get_footer(struct block *blk) {
  return ((void *)blk + WSIZE * blk->header.size) - sizeof(struct boundary_tag);
}

/* Set a block's size and inuse bit in header and footer */
static void set_header_and_footer(struct block *blk, int size, int inuse) {
  blk->header.inuse = inuse;
  blk->header.size = size;
  *get_footer(blk) = blk->header; /* Copy header to footer */
}

/* Mark a block as used and set its size. */
static void mark_block_used(struct block *blk, int size) {
  set_header_and_footer(blk, size, 1);
}

/* Mark a block as free and set its size. */
static void mark_block_free(struct block *blk, int size) {
  set_header_and_footer(blk, size, 0);
}

/* pushes to the corect segregated list based on size class*/
static void push_seg_list(struct block *bp) {
  if (bp->header.size >= SL9) {
    list_push_front(&seg_lists[9], &bp->elem);
  } else if (bp->header.size >= SL8) {
    list_push_front(&seg_lists[8], &bp->elem);
  } else if (bp->header.size >= SL7) {
    list_push_front(&seg_lists[7], &bp->elem);
  } else if (bp->header.size >= SL6) {
    list_push_front(&seg_lists[6], &bp->elem);
  } else if (bp->header.size >= SL5) {
    list_push_front(&seg_lists[5], &bp->elem);
  } else if (bp->header.size >= SL4) {
    list_push_front(&seg_lists[4], &bp->elem);
  } else if (bp->header.size >= SL3) {
    list_push_front(&seg_lists[3], &bp->elem);
  } else if (bp->header.size >= SL2) {
    list_push_front(&seg_lists[2], &bp->elem);
  } else if (bp->header.size >= SL1) {
    list_push_front(&seg_lists[1], &bp->elem);
  } else if (bp->header.size >= SL0) {
    list_push_front(&seg_lists[0], &bp->elem);
  }
}

/*
 * mm_init - Initialize the memory manager
 */
int mm_init(void * start, size_t len) {
  ASSERT(offsetof(struct block, payload) == 4);
  ASSERT(sizeof(struct boundary_tag) == 4);

  mem_brk = (char *)start;
  mem_max_addr = mem_brk + len;

  /* Create the initial empty heap */
  struct boundary_tag *initial = mem_sbrk(4 * sizeof(struct boundary_tag));
  if (initial == NULL)
    return -1;

  /* We use a slightly different strategy than suggested in the book.
   * Rather than placing a min-sized prologue block at the beginning
   * of the heap, we simply place two fences.
   * The consequence is that coalesce() must call prev_blk_footer()
   * and not prev_blk() because prev_blk() cannot be called on the
   * left-most block.
   */
  initial[2] = FENCE; /* Prologue footer */
  heap_listp = (struct block *)&initial[3];
  initial[3] = FENCE; /* Epilogue header */

  for (int i = 0; i < NUM_LISTS; i++) {
    list_init(&seg_lists[i]);
  }
  /* Extend the empty heap with a free block of CHUNKSIZE bytes */
  if (extend_heap(CHUNKSIZE) == NULL)
    return -1;
  return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
 */
void *mm_malloc(size_t size) {
//   ASSERT(mm_checkheap());
  struct block *bp;

  /* Ignore spurious requests */
  if (size == 0)
    return NULL;

  if (size % 7 == 0) {
    size = (size / 7) * 8;
  }

  /* Adjust block size to include overhead and alignment reqs. */
  size_t bsize =
      align(size + 2 * sizeof(struct boundary_tag)); /* account for tags */

    ASSERT(bsize % 16 == 0);
    
  if (bsize < size)
    return NULL; /* integer overflow */

  /* Adjusted block size in words */
  size_t awords =
      max(MIN_BLOCK_SIZE_WORDS, bsize / WSIZE); /* respect minimum size */

  /* Search the free list for a fit */
  if ((bp = find_fit(awords)) != NULL) {
    place(bp, awords);
    return bp->payload;
  }

  /* No fit found. Get more memory and place the block */
  size_t extendwords =
      max(awords, CHUNKSIZE); /* Amount to extend heap if no fit */
  if ((bp = extend_heap(extendwords)) == NULL)
    return NULL;

  place(bp, awords);
//   ASSERT(mm_checkheap());
  return bp->payload;
}

/*
 * mm_free - Free a block
 */
void mm_free(void *bp) {
//   ASSERT(mm_checkheap());
  ASSERT(heap_listp != 0); // ASSERT that mm_init was called
  if (bp == 0)
    return;

  /* Find block from user pointer */
  struct block *blk = bp - offsetof(struct block, payload);

  mark_block_free(blk, blk_size(blk));
  coalesce(blk);
//   ASSERT(mm_checkheap());
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static struct block *coalesce(struct block *bp) {
  bool prev_alloc =
      prev_blk_footer(bp)->inuse;            /* is previous block allocated? */
  bool next_alloc = !blk_free(next_blk(bp)); /* is next block allocated? */
  size_t size = blk_size(bp);

  if (prev_alloc && next_alloc) { /* Case 1 */
                                  // both are allocated, nothing to coalesce
    push_seg_list(bp);

    return bp;
  }

  else if (prev_alloc && !next_alloc) { /* Case 2 */
    // combine this block and next block by extending it
    list_remove(&next_blk(bp)->elem);
    mark_block_free(bp, size + blk_size(next_blk(bp)));
    push_seg_list(bp);
  }

  else if (!prev_alloc && next_alloc) { /* Case 3 */
    // combine previous and this block by extending previous
    bp = prev_blk(bp);
    list_remove(&bp->elem);
    mark_block_free(bp, size + blk_size(bp));
    push_seg_list(bp);
  } else { /* Case 4 */
           // combine all previous, this, and next block into one

    list_remove(&prev_blk(bp)->elem);
    list_remove(&next_blk(bp)->elem);

    mark_block_free(prev_blk(bp),
                    size + blk_size(next_blk(bp)) + blk_size(prev_blk(bp)));
    bp = prev_blk(bp);
    push_seg_list(bp);
  }

  ASSERT(blk_free(bp));

  return bp;
}

/*
 * The remaining routines are internal helper routines
 */

/*
 * extend_heap - Extend heap with free block and return its block pointer
 */
static struct block *extend_heap(size_t words) {
  void *bp = mem_sbrk(words * WSIZE);

  if (bp == NULL)
    return NULL;

  /* Initialize free block header/footer and the epilogue header.
   * Note that we overwrite the previous epilogue here. */
  struct block *blk = bp - sizeof(FENCE);
  mark_block_free(blk, words);
  next_blk(blk)->header = FENCE;

  push_seg_list(blk);
  return blk;
}

/*
 * place - Place block of asize words at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
static void place(struct block *bp, size_t asize) {
  size_t csize = blk_size(bp);

  if ((csize - asize) >= MIN_BLOCK_SIZE_WORDS) {
    mark_block_used(bp, asize);
    list_remove(&bp->elem);
    bp = next_blk(bp);
    mark_block_free(bp, csize - asize);
    push_seg_list(bp);
    ASSERT(blk_free(bp)); // temp checker

  } else {
    list_remove(&bp->elem);
    mark_block_used(bp, csize);
    ASSERT(!blk_free(bp));
  }
}

/*
 * find_fit - Find a fit for a block with asize words
 */
static struct block *find_fit(size_t asize) {

  if (asize < SL1 && !list_empty(&seg_lists[0])) {
    return find_fit_helper(0, asize);
  } else if (asize < SL2 && !list_empty(&seg_lists[1])) {
    return find_fit_helper(1, asize);
  } else if (asize < SL3 && !list_empty(&seg_lists[2])) {
    return find_fit_helper(2, asize);
  } else if (asize < SL4 && !list_empty(&seg_lists[3])) {
    return find_fit_helper(3, asize);
  } else if (asize < SL5 && !list_empty(&seg_lists[4])) {
    return find_fit_helper(4, asize);
  } else if (asize < SL6 && !list_empty(&seg_lists[5])) {
    return find_fit_helper(5, asize);
  } else if (asize < SL7 && !list_empty(&seg_lists[6])) {
    return find_fit_helper(6, asize);
  } else if (asize < SL8 && !list_empty(&seg_lists[7])) {
    return find_fit_helper(7, asize);
  } else if (asize < SL9 && !list_empty(&seg_lists[8])) {
    return find_fit_helper(8, asize);
  }

  return find_fit_helper(9, asize);
}

// helper for find fit—iterates through the seg lists based on size and gives
// the right block if possible after a certain number of attempts
static struct block *find_fit_helper(int sl_index, size_t asize) {
  for (int i = sl_index; i < NUM_LISTS; i++) {
    if (!list_empty(&seg_lists[i])) {
      int count = 0;
      for (struct list_elem *e = list_begin(&seg_lists[i]);
           e != list_end(&seg_lists[i]); e = list_next(e)) {

        struct block *bp = list_entry(e, struct block, elem);
        if (blk_free(bp) && asize <= blk_size(bp)) {
          return bp;
        }
        count++;
        if (count > 8) {
          break;
        }
      }
    }
  }
  return NULL;
}

void * _mm_malloc(size_t size, pthread_mutex_t lock)
{
    pthread_mutex_lock(&lock);
    void * ret = mm_malloc(size);
    pthread_mutex_unlock(&lock);
    return ret;
}

void _mm_free (void *ptr, pthread_mutex_t lock)
{
    pthread_mutex_lock(&lock);
    mm_free(ptr);
    pthread_mutex_unlock(&lock);
}


team_t team = {
    /* Team name */
    "Back's Bakas",
    /* First member's full name */
    "Thomas Tran",
    "thomastran@vt.edu",
    /* Second member's full name (leave as empty strings if none) */
    "Pierre Tran",
    "pierretran@vt.edu",
};
