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
