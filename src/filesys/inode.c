#include "filesys/inode.h"
#include "filesys/cache.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include <stdio.h>
#include "threads/thread.h"

// #include "lib/stdio.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_COUNT 123
#define INDIRECT_BLOCK_COUNT 128
#define GAP_MARKER UINT32_MAX - 1 // sparse files

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  bool isdir;
  uint8_t unused[3]; /* Not used. */
  off_t length;      /* File size in bytes. */
  unsigned magic;    /* Magic number. */
  block_sector_t direct_blocks[DIRECT_BLOCK_COUNT];
  block_sector_t L1_indirect_sector;
  block_sector_t L2_indirect_sector;
};

static void init_inode_disk(struct inode_disk* data, off_t length, bool isdir);
static block_sector_t install_file_sector(struct inode* inode, off_t offset);
static bool install_l1_indirect_block(off_t offset, struct inode_disk* id_data, UNUSED block_sector_t* l2_data);
static bool install_l2_indirect_block(struct inode_disk* id_data);
static block_sector_t calculate_file_sector(off_t offset);
static block_sector_t calculate_l1_index(off_t offset, bool l2_parent);
static block_sector_t calculate_l2_index(off_t offset);
static void set_gap_marker(block_sector_t* data, block_sector_t length);
static void install_indirect_init(block_sector_t sector);
static void install_file_sector_helper(block_sector_t indirect_sector, block_sector_t disk_sector, off_t offset);
static bool install_indirect_check(struct cache_block* parent_cb, struct inode_disk* id_data, UNUSED block_sector_t* l2_data,block_sector_t indirect_sector, block_sector_t disk_sector, off_t offset, bool is_L1);  
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */

  // flag to allocate or not
  static block_sector_t
  byte_to_sector(const struct inode *inode, off_t pos)
  {
    ASSERT(inode != NULL);

    struct cache_block *cb = cache_get_block(inode->sector, false);
    struct inode_disk *data = (struct inode_disk *)cache_read_block(cb);

    block_sector_t pos_sector = GAP_MARKER;
    block_sector_t file_sector = calculate_file_sector(pos);

    // we don't support beyond this
    if (file_sector >= (block_sector_t)(DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT + (INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT)))
    {
      return GAP_MARKER;
    }

    // dir
    if (file_sector < (block_sector_t)DIRECT_BLOCK_COUNT)
    {
      cache_put_block(cb);
      return data->direct_blocks[file_sector];
    }

    // indir
    else if (file_sector < (block_sector_t)(DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT))
    {
      if (data->L1_indirect_sector == GAP_MARKER)
      {
        cache_put_block(cb);
        return GAP_MARKER;
      }
      else
      {
        struct cache_block *cb_indirectL1 = cache_get_block(data->L1_indirect_sector, false);
        block_sector_t *indirect1_sector = (block_sector_t *)cache_read_block(cb_indirectL1);
        pos_sector = indirect1_sector[calculate_l1_index(pos, false)];
        cache_put_block(cb_indirectL1);
        cache_put_block(cb);
        return pos_sector;
      }
    }

    // doubly
    if (data->L2_indirect_sector == GAP_MARKER)
    {
      cache_put_block(cb);
      return GAP_MARKER;
    }
    off_t doubly_indirect_index = calculate_l2_index(pos);
    off_t indirect_index = calculate_l1_index(pos, true);

    struct cache_block *cb_indirectL2 = cache_get_block(data->L2_indirect_sector, false);
    block_sector_t *indirectL2_data = (block_sector_t *)cache_read_block(cb_indirectL2);

    block_sector_t indirectL1_sector = indirectL2_data[doubly_indirect_index];
    if (indirectL1_sector == GAP_MARKER)
    {
      cache_put_block(cb_indirectL2);
      cache_put_block(cb);
      return GAP_MARKER;
    }

    struct cache_block *cb_indirectL1 = cache_get_block(indirectL1_sector, false);
    block_sector_t *indirect1_sector = cache_read_block(cb_indirectL1);
    pos_sector = indirect1_sector[indirect_index];

    cache_put_block(cb_indirectL2);
    cache_put_block(cb_indirectL1);
    cache_put_block(cb);
    return pos_sector;
  }


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock list_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init(&list_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
    one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      init_inode_disk(disk_inode, length, isdir);

      ASSERT(sector != GAP_MARKER && sector != UINT32_MAX);

      struct cache_block* cb = cache_get_block(sector, true);
      void *data_ptr = cache_zero_block(cb);
      memcpy(data_ptr, disk_inode, BLOCK_SECTOR_SIZE);
      cache_mark_dirty(cb);
      cache_put_block(cb);

      free (disk_inode);
      success = true;
    }
  return success;
}   

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire(&list_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
      e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      lock_release(&list_inodes_lock);

      // lock_acquire(&inode->lock);

      // sector never changes
      // not in lru list before inode fields are initialized
      if (inode->sector == sector)
      {
        inode_reopen(inode);
        // lock_release(&inode->lock);
        return inode;
      }
      // lock_release(&inode->lock);
      lock_acquire(&list_inodes_lock);
    }
    lock_release(&list_inodes_lock);

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL)
      return NULL;

    /* Initialize. */
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    lock_init(&inode->lock);

    ASSERT(sector != GAP_MARKER && sector != UINT32_MAX);

    lock_acquire(&list_inodes_lock);
    list_push_front(&open_inodes, &inode->elem);
    lock_release(&list_inodes_lock);
    return inode;
}
   

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
  {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire(&inode->lock);
  if (--inode->open_cnt == 0)
    {
      lock_release(&inode->lock);

      /* Remove from inode list and release lock. */
      lock_acquire(&list_inodes_lock);
      list_remove (&inode->elem);
      lock_release(&list_inodes_lock);

      /* Deallocate blocks if removed. */
      lock_acquire(&inode->lock);
      if (inode->removed) 
        {
          ASSERT(inode->sector != GAP_MARKER && inode->sector != UINT32_MAX);

          struct cache_block* cb = cache_get_block(inode->sector, false);
          struct inode_disk* data =  (struct inode_disk *)cache_read_block(cb);
          int length = data->length;
          cache_put_block(cb);
          lock_release(&inode->lock);

          // review
          for(size_t i = 0; i < bytes_to_sectors(length); ++i){
            block_sector_t curr_sector = byte_to_sector(inode, i * BLOCK_SECTOR_SIZE);

            ASSERT(curr_sector != UINT32_MAX);

            if(curr_sector == GAP_MARKER){
              continue;
            }
            else
            {
              free_map_release(curr_sector, 1);
            }
          }
          free_map_release(inode->sector, 1);
        }
        else
        {
          lock_release(&inode->lock);
        }
      free (inode);
    }
    else
    {
      lock_release(&inode->lock);
    }
}
   

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  lock_acquire(&inode->lock);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      off_t length = inode_length(inode);
      if(offset >= length){
        lock_release(&inode->lock);
        return 0;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if(sector_idx == GAP_MARKER){
        static char zeros[BLOCK_SECTOR_SIZE];
        memcpy(buffer + bytes_read, zeros, chunk_size);
      }
      else
      {
        ASSERT(sector_idx != GAP_MARKER && sector_idx != UINT32_MAX);

        struct cache_block* cb = cache_get_block(sector_idx, false);
        void *data = cache_read_block(cb);
        memcpy(buffer + bytes_read, data+sector_ofs, chunk_size);
        cache_put_block(cb);
      }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  lock_release(&inode->lock);
  return bytes_read;
}
   

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  lock_acquire(&inode->lock);
  if (inode->deny_write_cnt){
    lock_release(&inode->lock);
    return 0;
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      // off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      // int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      // int chunk_size = size < min_left ? size : min_left;
      int chunk_size = size < sector_left ? size : sector_left;

      if (chunk_size <= 0)
        break;
      
      struct cache_block* cb = NULL;
      off_t curr_length = inode_length(inode);

      // either for eof/extend case, or sparse
      // need to install
      if(sector_idx == GAP_MARKER){
        if(!install_file_sector(inode, offset)){
          lock_release(&inode->lock);
          return 0;
        }
        // installed, so should have non-GAP MARKER sector idx
        sector_idx = byte_to_sector(inode, offset);
        ASSERT(sector_idx != GAP_MARKER);
        cb = cache_get_block(sector_idx, true);
      }
      else {
        // could make this not excl?
        // doesn't have to be excl, no guaranteee about file data
        cb = cache_get_block(sector_idx, false);
      }

      ASSERT(cb != NULL);
      struct inode_disk * data = NULL;
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      {
        /* fully overwritten */
        data = (struct inode_disk*)cache_zero_block(cb);
      }
      else
      {
        /* partial write */
        data = (struct inode_disk*)cache_read_block(cb);
      }

      memcpy(((void*)data) + sector_ofs, buffer + bytes_written, chunk_size);

      cache_mark_dirty(cb);
      cache_put_block(cb); 

      // length? size or chunksize
      if (offset + chunk_size > curr_length) {
        cb = cache_get_block(inode->sector, true);
        struct inode_disk* disk_inode = (struct inode_disk*)cache_read_block(cb);
        disk_inode->length = offset + chunk_size;
        cache_mark_dirty(cb);
        cache_put_block(cb);
      }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  lock_release(&inode->lock);
  return bytes_written;
}
   

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&inode->lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  ASSERT(inode->sector != GAP_MARKER);
  struct cache_block* cb = cache_get_block(inode->sector, false);
  struct inode_disk* data = cache_read_block(cb);
  int length = data->length;
  cache_put_block(cb);
  return length;
}


static void init_inode_disk(struct inode_disk* data, off_t length, bool isdir){
  set_gap_marker(data->direct_blocks, (block_sector_t)DIRECT_BLOCK_COUNT);
  data->L1_indirect_sector = GAP_MARKER;
  data->L2_indirect_sector = GAP_MARKER;
  data->length = length;
  data->magic = INODE_MAGIC;
  data->isdir = isdir;
}

static block_sector_t install_file_sector(struct inode* inode, off_t offset){
  ASSERT(inode != NULL);
  block_sector_t file_sector = calculate_file_sector(offset);
  block_sector_t disk_sector;

  if(!free_map_allocate(1, &disk_sector)){
    return false;
  }
  
  block_sector_t id_sector = inode->sector;
  ASSERT(id_sector != GAP_MARKER);
  struct cache_block* id_cb = cache_get_block(id_sector, false);
  ASSERT(id_cb != NULL);
  struct inode_disk* id_data = (struct inode_disk*)cache_read_block(id_cb);
  ASSERT(id_data != NULL);

  // dir
  if(file_sector < (block_sector_t) DIRECT_BLOCK_COUNT){
    id_data->direct_blocks[file_sector] =  disk_sector;
    cache_mark_dirty(id_cb);
    cache_put_block(id_cb);
  }

  // l1
  else if(file_sector < (block_sector_t)(DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT)){
    if(!install_indirect_check(id_cb, id_data, NULL, id_data->L1_indirect_sector, disk_sector, offset, true)){
      return false;
    }
    ASSERT(id_data->L1_indirect_sector != GAP_MARKER);
    
    install_file_sector_helper(id_data->L1_indirect_sector, disk_sector, offset);

    cache_put_block(id_cb);
  }

  // l2
  else{
    if(!install_indirect_check(id_cb, id_data, NULL, id_data->L2_indirect_sector, disk_sector, offset, false)){
      return false;
    }
    ASSERT(id_data->L2_indirect_sector != GAP_MARKER);

    struct cache_block* l2_cb = cache_get_block(id_data->L2_indirect_sector, false);
    ASSERT(l2_cb != NULL);
    block_sector_t* l2_data = cache_read_block(l2_cb); // cast??
    ASSERT(l2_data != NULL);

    block_sector_t l2_index = calculate_l2_index(offset);
    if(!install_indirect_check(l2_cb, id_data, l2_data, l2_data[l2_index], disk_sector, offset, true)){
      free_map_release(id_data->L2_indirect_sector, 1);
      return false;
    }
    ASSERT(l2_data[l2_index] != GAP_MARKER);
    
    install_file_sector_helper(l2_data[l2_index], disk_sector, offset);
    
    cache_put_block(l2_cb);
    cache_put_block(id_cb);
  }

  return true;
}

static bool install_l1_indirect_block(off_t offset, struct inode_disk* id_data, UNUSED block_sector_t* l2_data){
  block_sector_t file_sector = calculate_file_sector(offset);
  ASSERT(id_data != NULL);
  
  if(file_sector < ((block_sector_t)(DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT))){
    ASSERT(id_data->L1_indirect_sector == GAP_MARKER);
    ASSERT(l2_data == NULL);
    if(!free_map_allocate(1, &id_data->L1_indirect_sector)){
      return false;
    } 
    ASSERT(id_data->L1_indirect_sector != GAP_MARKER);

    install_indirect_init(id_data->L1_indirect_sector);
  }  
  else{
    ASSERT(id_data->L2_indirect_sector != GAP_MARKER);
    off_t l2_index = calculate_l2_index(offset);
    ASSERT(l2_index >=0);
    ASSERT(l2_data != NULL);
    ASSERT(l2_data[l2_index] == GAP_MARKER);  

    if(!free_map_allocate(1, &l2_data[l2_index])){
      free_map_release(id_data->L2_indirect_sector, 1);
      return false;
    }

    install_indirect_init(l2_data[l2_index]);
  }
  return true;
}


static bool install_l2_indirect_block(struct inode_disk* id_data){
  ASSERT(id_data->L2_indirect_sector == GAP_MARKER);

  if(!free_map_allocate(1, &id_data->L2_indirect_sector)){
    return false;
  }
  install_indirect_init(id_data->L2_indirect_sector);
  return true;
}

bool is_dir(struct inode * inode){
  struct cache_block* cb = cache_get_block(inode->sector, false);
  struct inode_disk* data = (struct inode_disk*)cache_read_block(cb);
  bool isdir = data->isdir;
  cache_put_block(cb);
  return isdir;
}

static block_sector_t calculate_file_sector(off_t offset){
  return (((block_sector_t)offset) / ((block_sector_t)BLOCK_SECTOR_SIZE));
}

static block_sector_t calculate_l1_index(off_t offset, bool l2_parent){
  if(l2_parent){
    return ((calculate_file_sector(offset) - ((block_sector_t) DIRECT_BLOCK_COUNT) - ((block_sector_t) INDIRECT_BLOCK_COUNT)) % ((block_sector_t) INDIRECT_BLOCK_COUNT));
  }
  else{
    return (calculate_file_sector(offset) - ((block_sector_t) DIRECT_BLOCK_COUNT));
  }

}

static block_sector_t calculate_l2_index(off_t offset){
  return ((calculate_file_sector(offset) - ((block_sector_t) DIRECT_BLOCK_COUNT) - ((block_sector_t) INDIRECT_BLOCK_COUNT)) /((block_sector_t) INDIRECT_BLOCK_COUNT));
}

static void install_file_sector_helper(block_sector_t indirect_sector, block_sector_t disk_sector, off_t offset){
  struct cache_block* l1_cb = cache_get_block(indirect_sector, false);
  block_sector_t* l1_data = cache_read_block(l1_cb);
  block_sector_t l1_index = calculate_l1_index(offset, true);
  l1_data[l1_index] = disk_sector;
  cache_mark_dirty(l1_cb);

  cache_put_block(l1_cb);


}

static void install_indirect_init(block_sector_t sector){
  struct cache_block* indirect_cb = cache_get_block(sector ,true);
  block_sector_t* indirect_data = cache_zero_block(indirect_cb);

  set_gap_marker(indirect_data, (block_sector_t)INDIRECT_BLOCK_COUNT);
  cache_mark_dirty(indirect_cb);
  cache_put_block(indirect_cb);
}

static bool install_indirect_check(struct cache_block* parent_cb, struct inode_disk* id_data, UNUSED block_sector_t* l2_data,block_sector_t indirect_sector, block_sector_t disk_sector, off_t offset, bool is_L1){
  if(indirect_sector == GAP_MARKER){
    bool installed = is_L1 ? install_l1_indirect_block(offset, id_data, l2_data): install_l2_indirect_block(id_data);
    if(!installed){
      free_map_release(disk_sector, 1);
      return false;
    }
    cache_mark_dirty(parent_cb);
  }
  return true;
}

static void set_gap_marker(block_sector_t* data, block_sector_t length){
  ASSERT(data != NULL);
  for(block_sector_t i = 0; i < length; ++i){
    data[i] =  GAP_MARKER;
  }
}
