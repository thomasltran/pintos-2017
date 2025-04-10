#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include <stdio.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  bool is_dir;
  uint8_t unused[3];
  off_t length;   /* File size in bytes. */
  unsigned magic; /* Magic number. */

  block_sector_t direct[123];
  // support for 123 blocks
  // 123 * 512 = 62976 bytes

  block_sector_t indirect;
  // support for 128 (from 512/4 = 128) blocks
  // 128 * 512 = 65536 bytes

  block_sector_t doubly_indirect;
  // support for 128*128 (from 512/4 = 128 indirect indices, times 128 blocks within each one = 16384) blocks
  // 16384 * 512 = 8388608 bytes
};

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
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT(inode != NULL);

  struct cache_block *cb = cache_get_block(inode->sector, false);
  struct inode_disk *data = (struct inode_disk *)cache_read_block(cb);

  off_t length = data->length;
  block_sector_t * direct_ref = data->direct;
  block_sector_t indirect_ref = data->indirect;
  block_sector_t doubly_indirect_ref = data->doubly_indirect;

  cache_put_block(cb);

  if (pos >= length){
    return -1;
  }
    
  off_t file_sector = pos / BLOCK_SECTOR_SIZE;

  if (file_sector < 123) // 0 - 122 is direct
  {
    return direct_ref[file_sector];
  }
  else if (file_sector < 123 + 128) // 123 - 250 is indirect
  {
    cb = cache_get_block(indirect_ref, false);
    block_sector_t *indirect_block = (block_sector_t *)cache_read_block(cb);
    block_sector_t sector = indirect_block[file_sector - 123]; // remove direct overhead from equation
    cache_put_block(cb);
    return sector;
  }

  // doubly_indirect -> index within the doubly_indirect block (indirect) -> index within the indirect block (sector)
  off_t doubly_indirect = file_sector - 123 - 128; // remove direct and indirect overhead from equation
  off_t doubly_indirect_index = doubly_indirect / 128;
  off_t indirect_index = doubly_indirect % 128;

  cb = cache_get_block(doubly_indirect_ref, false);
  block_sector_t *doubly_indirect_block = (block_sector_t *)cache_read_block(cb);
  block_sector_t indirect_sector = doubly_indirect_block[doubly_indirect_index];
  cache_put_block(cb);

  cb = cache_get_block(indirect_sector, false);
  block_sector_t *indirect_block = (block_sector_t *)cache_read_block(cb);
  block_sector_t sector = indirect_block[indirect_index];
  cache_put_block(cb);

  return sector;
}
   
/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
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
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          struct cache_block * cb = cache_get_block(sector, true); // same as start, what about memcpy
          uint8_t * data_ptr = cache_zero_block(cb);
          memcpy(data_ptr, disk_inode, BLOCK_SECTOR_SIZE);
          cache_mark_dirty(cb);
          
          cache_put_block(cb);
          
          if (sectors > 0) 
            {
              size_t i;
              
              for (i = 0; i < sectors; i++) {
                cb = cache_get_block(disk_inode->start + i, true);
                cache_zero_block(cb);
                cache_mark_dirty(cb);
                cache_put_block(cb);
              }
            }
          success = true; 
        } 
      free (disk_inode);
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
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  struct cache_block* cb = cache_get_block(sector, false);
  cache_read_block(cb); //memcpy?
  cache_put_block(cb);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
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
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct cache_block* cb = cache_get_block(inode->sector, false);
          struct inode_disk* data = (struct inode_disk *)cache_read_block(cb);
          int length = data->length; 
          int start = data->start;
          cache_put_block(cb);

          free_map_release (inode->sector, 1);
          free_map_release (start,
                            bytes_to_sectors (length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct cache_block* cb = cache_get_block(sector_idx, false);
      void* data = cache_read_block(cb);

      memcpy(buffer + bytes_read, data+sector_ofs, chunk_size);
      cache_put_block(cb);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

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

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct cache_block * cb = cache_get_block(sector_idx, true);
      // write is excl
      uint8_t * data_ptr; // returned from zero or read

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* fully overwritten */
          data_ptr = cache_zero_block(cb);
        }
      else 
        {
          /* partial write */
          data_ptr = cache_read_block(cb);
        }

      memcpy (data_ptr + sector_ofs, buffer + bytes_written, chunk_size);

      cache_mark_dirty(cb);
      cache_put_block(cb);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct cache_block * cb = cache_get_block(inode->sector, false);
  struct inode_disk * data = (struct inode_disk*)cache_read_block(cb);
  off_t length = data->length;
  cache_put_block(cb);
  return length;
}







----------

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

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_COUNT 123
#define INDIRECT_BLOCK_COUNT 128
#define GAP_MARKER (UINT32_MAX - 1)

/* On-disk inode.
Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  bool is_dir;
  uint8_t unused[3];
  off_t length;   /* File size in bytes. */
  unsigned magic; /* Magic number. */

  block_sector_t direct[DIRECT_BLOCK_COUNT];
  // support for 123 blocks
  // 123 * 512 = 62976 bytes

  block_sector_t indirect;
  // support for 128 (from 512/4 = 128) blocks
  // 128 * 512 = 65536 bytes

  block_sector_t doubly_indirect;
  // support for 128*128 (from 512/4 = 128 indirect indices, times 128 blocks within each one = 16384) blocks
  // 16384 * 512 = 8388608 bytes
};
   
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
};

static void init_inode_disk(struct inode_disk *data, off_t length);
static void install_file_sector(struct inode *inode, off_t offset);
static void install_l1_indirect_block(struct inode *inode, UNUSED off_t offset);
static void install_l2_indirect_block(struct inode *inode);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT(inode != NULL);

  struct cache_block *cb = cache_get_block(inode->sector, false);
  struct inode_disk *data = (struct inode_disk *)cache_read_block(cb);

  off_t length = data->length;
  block_sector_t * direct_ref = data->direct;
  block_sector_t indirect_ref = data->indirect;
  block_sector_t doubly_indirect_ref = data->doubly_indirect;

  cache_put_block(cb);

  if (pos >= length){
    return -1;
  }

  off_t file_sector = pos / BLOCK_SECTOR_SIZE;

  // direct
  if (file_sector < DIRECT_BLOCK_COUNT)
  {
    return direct_ref[file_sector];
  }

  // indirect
  else if (file_sector < DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT)
  {
    // sparsely allocated
    if (data->indirect == GAP_MARKER)
    {
      return GAP_MARKER;
    }

    cb = cache_get_block(indirect_ref, false);
    block_sector_t *indirect_block = (block_sector_t *)cache_read_block(cb);
    block_sector_t sector = indirect_block[file_sector - DIRECT_BLOCK_COUNT];
    cache_put_block(cb);
    return sector;
  }

  // doubly indirect
  // sparsely allocated
  if (data->doubly_indirect == GAP_MARKER)
  {
    return GAP_MARKER;
  }

  // doubly_indirect -> index within the doubly_indirect block (indirect) -> index within the indirect block (sector)
  off_t doubly_indirect = file_sector - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_COUNT; // remove direct and indirect overhead from equation
  off_t doubly_indirect_index = doubly_indirect / INDIRECT_BLOCK_COUNT;
  off_t indirect_index = doubly_indirect % INDIRECT_BLOCK_COUNT;

  cb = cache_get_block(doubly_indirect_ref, false);
  block_sector_t *doubly_indirect_block = (block_sector_t *)cache_read_block(cb);
  block_sector_t indirect_sector = doubly_indirect_block[doubly_indirect_index];

  // sparsely allocated
  if (indirect_sector == GAP_MARKER)
  {
    cache_put_block(cb);
    return GAP_MARKER;
  }
  cache_put_block(cb);

  cb = cache_get_block(indirect_sector, false);
  block_sector_t *indirect_block = (block_sector_t *)cache_read_block(cb);
  block_sector_t sector = indirect_block[indirect_index];
  cache_put_block(cb);

  return sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
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
    init_inode_disk(disk_inode, length);

    struct cache_block *cb = cache_get_block(sector, true);
    cache_zero_block(cb);
    cache_mark_dirty(cb);
    void *data = cache_read_block(cb);

    memcpy(data, disk_inode, BLOCK_SECTOR_SIZE);
    cache_put_block(cb);

    free(disk_inode);
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
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  struct cache_block* cb = cache_get_block(sector, false);
  cache_read_block(cb);
  cache_put_block(cb);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
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
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct cache_block* cb = cache_get_block(inode->sector, false);
          struct inode_disk *data = (struct inode_disk *)cache_read_block(cb);
          int length = data->length;
          cache_put_block(cb);
          for (size_t i = 0; i < bytes_to_sectors(length); ++i)
          {
            block_sector_t curr_sector = byte_to_sector(inode, i * BLOCK_SECTOR_SIZE);
            ASSERT(curr_sector != (block_sector_t)-1);
            if (curr_sector == GAP_MARKER)
            {
              continue;
            }
            else
            {
              free_map_release(curr_sector, 1);
            }
          }
          free_map_release(inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == (block_sector_t)-1)
      {
        return 0;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      if (sector_idx == GAP_MARKER)
      {
        static char zeros[BLOCK_SECTOR_SIZE];
        memcpy(buffer + bytes_read, zeros, chunk_size);
      }
      else
      {
        struct cache_block *cb = cache_get_block(sector_idx, false);
        void *data = cache_read_block(cb);

        memcpy(buffer + bytes_read, data + sector_ofs, chunk_size);
        cache_put_block(cb);
      }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

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
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_idx == (block_sector_t)-1)
      {
        struct cache_block *id_cb = cache_get_block(inode->sector, true);
        struct inode_disk *id_data = cache_read_block(id_cb);
        ASSERT(id_data->length < offset + size)
        id_data->length = offset + size;
        cache_mark_dirty(id_cb);
        cache_put_block(id_cb);

        install_file_sector(inode, offset);
        sector_idx = byte_to_sector(inode, offset);
        ASSERT(sector_idx != GAP_MARKER && sector_idx != (block_sector_t)-1);
        struct cache_block *cb = cache_get_block(sector_idx, true);
        void *data = cache_read_block(cb);
        memcpy(data + sector_ofs, buffer + bytes_written, chunk_size);
        cache_mark_dirty(cb);
        cache_put_block(cb);
      }
      else if (sector_idx == GAP_MARKER)
      {
        install_file_sector(inode, offset);
        sector_idx = byte_to_sector(inode, offset);
        ASSERT(sector_idx != GAP_MARKER && sector_idx != (block_sector_t)-1);
        struct cache_block *cb = cache_get_block(sector_idx, true);
        void *data = cache_read_block(cb);
        memcpy(data + sector_ofs, buffer + bytes_written, chunk_size);
        cache_mark_dirty(cb);
        cache_put_block(cb);
      }
      else
      {
        struct cache_block *cb = cache_get_block(sector_idx, true);
        void *data = cache_read_block(cb);
        memcpy(data + sector_ofs, buffer + bytes_written, chunk_size);
        cache_mark_dirty(cb);
        cache_put_block(cb);
      }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
    free(bounce);

    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct cache_block *cb = cache_get_block(inode->sector, false);
  struct inode_disk *data = cache_read_block(cb);
  int length = data->length;
  cache_put_block(cb);
  return length;
}

static void init_inode_disk(struct inode_disk *data, off_t length)
{
  for (int i = 0; i < DIRECT_BLOCK_COUNT; ++i)
  {
    data->direct[i] = GAP_MARKER;
  }
  data->indirect = GAP_MARKER;
  data->doubly_indirect = GAP_MARKER;
  data->length = length;
  data->magic = INODE_MAGIC;
}

static void install_file_sector(struct inode *inode, off_t offset)
{
  ASSERT(inode != NULL);
  block_sector_t file_sector = offset / BLOCK_SECTOR_SIZE;
  block_sector_t disk_sector = GAP_MARKER;
  bool already_held = true;
  free_map_allocate(1, &disk_sector);
  block_sector_t id_sector = inode->sector;

  if (file_sector < DIRECT_BLOCK_COUNT)
  {
    struct cache_block *id_cb = cache_get_block(id_sector, true);
    struct inode_disk *id_data = (struct inode_disk *)cache_read_block(id_cb);

    id_data->direct[file_sector] = disk_sector;
    cache_mark_dirty(id_cb);
    cache_put_block(id_cb);
  }
  else if (file_sector < DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT)
  {
    struct cache_block *id_cb = cache_get_block(id_sector, false);
    struct inode_disk *id_data = (struct inode_disk *)cache_read_block(id_cb);
    bool L1_present = id_data->indirect != GAP_MARKER;

    if (!L1_present)
    {
      cache_put_block(id_cb);
      install_l1_indirect_block(inode, offset);
      id_cb = cache_get_block(id_sector, false);
      id_data = (struct inode_disk *)cache_read_block(id_cb);
    }

    struct cache_block *l1_cb = cache_get_block(id_data->indirect, true);
    block_sector_t *l1_data = cache_read_block(l1_cb);

    block_sector_t l1_index = file_sector - DIRECT_BLOCK_COUNT;
    l1_data[l1_index] = disk_sector;

    cache_mark_dirty(l1_cb);
    cache_put_block(l1_cb);
    cache_put_block(id_cb);
  }
  else
  {
    struct cache_block *id_cb = cache_get_block(id_sector, false);
    ASSERT(id_cb != NULL);
    struct inode_disk *id_data = (struct inode_disk *)cache_read_block(id_cb);
    ASSERT(id_data != NULL);
    bool L2_present = id_data->doubly_indirect != GAP_MARKER;

    if (!L2_present)
    {
      cache_put_block(id_cb);
      install_l2_indirect_block(inode);
      id_cb = cache_get_block(id_sector, false);
      ASSERT(id_cb != NULL);
      id_data = (struct inode_disk *)cache_read_block(id_cb);
      ASSERT(id_data != NULL);
    }
    ASSERT(id_data->doubly_indirect != GAP_MARKER);

    struct cache_block *l2_cb = cache_get_block(id_data->doubly_indirect, false);
    ASSERT(l2_cb != NULL);
    block_sector_t *l2_data = cache_read_block(l2_cb);
    ASSERT(l2_data != NULL);

    block_sector_t l2_index = (file_sector - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_COUNT) / INDIRECT_BLOCK_COUNT;
    bool L1_present = l2_data[l2_index] != GAP_MARKER;
    if (!L1_present)
    {
      cache_put_block(l2_cb);

      cache_put_block(id_cb);

      install_l1_indirect_block(inode, offset);

      id_cb = cache_get_block(id_sector, false);
      ASSERT(id_cb != NULL);
      id_data = (struct inode_disk *)cache_read_block(id_cb);
      ASSERT(id_data != NULL);

      l2_cb = cache_get_block(id_data->doubly_indirect, false);
      ASSERT(l2_cb != NULL);
      l2_data = cache_read_block(l2_cb);
      ASSERT(l2_data != NULL);
    }

    ASSERT(l2_data[l2_index] != GAP_MARKER);

    struct cache_block *l1_cb = cache_get_block(l2_data[l2_index], true);
    block_sector_t *l1_data = cache_read_block(l1_cb);
    block_sector_t l1_index = (file_sector - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_COUNT) % INDIRECT_BLOCK_COUNT;
    l1_data[l1_index] = disk_sector;

    cache_mark_dirty(l1_cb);
    cache_put_block(l1_cb);
  }
  if (already_held)
  {
  }
}

static void install_l1_indirect_block(struct inode *inode, off_t offset)
{
  block_sector_t id_sector = inode->sector;
  if ((uint32_t)(offset / BLOCK_SECTOR_SIZE) < (DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT))
  {
    struct cache_block *id_cb = cache_get_block(id_sector, true);
    struct inode_disk *id_data = cache_read_block(id_cb);
    ASSERT(id_data->indirect == GAP_MARKER);

    free_map_allocate(1, &id_data->indirect);
    ASSERT(id_data->indirect != GAP_MARKER && id_data->indirect != (block_sector_t)-1);

    cache_mark_dirty(id_cb);

    struct cache_block *l1_cb = cache_get_block(id_data->indirect, true);
    block_sector_t *l1_data = cache_read_block(l1_cb);

    for (unsigned int i = 0; i < INDIRECT_BLOCK_COUNT; ++i)
    {
      l1_data[i] = GAP_MARKER;
    }
    cache_mark_dirty(l1_cb);
    cache_put_block(l1_cb);
    cache_put_block(id_cb);
  }
  else
  {
    struct cache_block *id_cb = cache_get_block(id_sector, false);
    struct inode_disk *id_data = cache_read_block(id_cb);

    bool l2_present = id_data->doubly_indirect;

    cache_put_block(id_cb);

    if (!l2_present)
    {
      install_l2_indirect_block(inode);
    }

    id_cb = cache_get_block(id_sector, false);
    id_data = cache_read_block(id_cb);

    struct cache_block *l2_cb = cache_get_block(id_data->doubly_indirect, true);
    block_sector_t *l2_data = cache_read_block(l2_cb);

    block_sector_t file_sector = offset / BLOCK_SECTOR_SIZE;
    off_t l2_index = (file_sector - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_COUNT) / INDIRECT_BLOCK_COUNT;
    ASSERT(l2_data[l2_index] == GAP_MARKER);

    free_map_allocate(1, &l2_data[l2_index]);

    cache_mark_dirty(l2_cb);

    struct cache_block *l1_cb = cache_get_block(l2_data[l2_index], true);
    block_sector_t *l1_data = cache_read_block(l1_cb);

    for (unsigned int i = 0; i < INDIRECT_BLOCK_COUNT; ++i)
    {
      l1_data[i] = GAP_MARKER;
    }
    cache_mark_dirty(l1_cb);
    cache_put_block(l1_cb);
    cache_put_block(l2_cb);
    cache_put_block(id_cb);
  }
}

static void install_l2_indirect_block(struct inode *inode)
{
  ASSERT(inode != NULL);
  block_sector_t id_sector = inode->sector;
  struct cache_block *id_cb = cache_get_block(id_sector, true);
  struct inode_disk *id_data = cache_read_block(id_cb);
  ASSERT(id_data->doubly_indirect == GAP_MARKER);

  free_map_allocate(1, &id_data->doubly_indirect);
  cache_mark_dirty(id_cb);

  struct cache_block *l2_cb = cache_get_block(id_data->doubly_indirect, true);
  block_sector_t *l2_data = cache_read_block(l2_cb);

  for (unsigned int i = 0; i < INDIRECT_BLOCK_COUNT; ++i)
  {
    l2_data[i] = GAP_MARKER;
  }

  cache_mark_dirty(l2_cb);
  cache_put_block(l2_cb);
  cache_put_block(id_cb);
}