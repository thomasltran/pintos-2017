#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "lib/string.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
  bool
  dir_create(block_sector_t sector, size_t entry_cnt, block_sector_t parent_sector)
  {
     if (!inode_create(sector, entry_cnt * sizeof(struct dir_entry), true))
     {
        return false;
     }

     struct dir *dir = dir_open(inode_open(sector));
     if (dir == NULL)
     {
        return false;
     }

     // adds .. and . and their corresponding sector as dir entries in the dir
     if (!dir_add(dir, "..", parent_sector))
     {
        dir_close(dir);
        return false;
     }

     if (!dir_add(dir, ".", sector))
     {
        dir_close(dir);
        return false;
     }

     dir_close(dir);
     return true;
  }

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  // printf("dir add: %s, added to dir: %u\n", name, inode_get_inumber(dir_get_inode(dir)));
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if (!check_empty(inode) || !dir_removable(inode))
  {
    goto done;
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;


  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use && strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0)
      {
        strlcpy(name, e.name, NAME_MAX + 1);
        // printf("dir pos %d\n", dir->pos);
        return true;
      }
    }

  return false;
}

// if false, don't have to close the cwd
// if true, up to caller to close
// opened inode is inode of the cwd at the end
bool resolve_path(char * path, char ** filename_ret, struct dir ** cwd)
{
   char *cpy = malloc(PATH_MAX + 1);
   if (cpy == NULL)
   {
      return false;
   }
   char *filename_cpy = NULL;

   strlcpy(cpy, path, PATH_MAX + 1);
   struct dir *curr_dir = NULL;
   struct thread *cur = thread_current();

   if (strlen(cpy) == 0)
   {
     free(cpy);
     return false;
   }

   // special case just "/"
   if (strcmp(cpy, "/") == 0)
   {
     curr_dir = dir_open_root();
     filename_cpy = malloc(NAME_MAX + 1);
     if (filename_cpy == NULL)
     {
       dir_close(curr_dir);
       free(cpy);
       return false;
     }
     strlcpy(filename_cpy, ".", NAME_MAX + 1);
     *filename_ret = filename_cpy;
     *cwd = curr_dir;
     free(cpy);
     return true;
   }

   // first char is /
   if (cpy[0] == '/')
   {
      curr_dir = dir_open_root();
   }
   // uses thread's curr dir
   else
   {
      curr_dir = dir_reopen(cur->curr_dir);
   }

   ASSERT(curr_dir != NULL);

   char *token, *save_ptr;
   char *filename = NULL;

   // parses by /
   token = strtok_r(cpy, "/", &save_ptr);
   while (token != NULL)
   {
      while (*token == ' ')
         token++;

      char *next = strtok_r(NULL, "/", &save_ptr);
      if (next == NULL)
      {
         filename = token;
         break;
      }

      struct inode *inode = NULL;

      if (strcmp(token, ".") != 0) // .
      {
         // returns a reopen vers of inode
         if (!dir_lookup(curr_dir, token, &inode))
         {
            dir_close(curr_dir);
            free(cpy);
            return false;
         }

         if (!is_dir(inode)) // file, but not at the end of path
         {
            inode_close(inode);
            dir_close(curr_dir);
            free(cpy);
            return false;
         }

         dir_close(curr_dir);

         // curr_dir represents inode, closing the dir closes inode too
         curr_dir = dir_open(inode);
         if (curr_dir == NULL)
         {
            free(cpy);
            return false;
         }
      }

      token = next;
   }

   // exceeds our limit
   if(filename == NULL || strlen(filename) > NAME_MAX + 1){
      dir_close(curr_dir);
      free(cpy);
      return false;
   }

   // ret version
   filename_cpy = malloc(NAME_MAX + 1);
   if (filename_cpy == NULL)
   {
      dir_close(curr_dir);
      free(cpy);
      return false;
   }
   strlcpy(filename_cpy, filename, NAME_MAX + 1);
	
	 *filename_ret = filename_cpy;
	 *cwd = curr_dir;

   free(cpy);
   return true;
}

// if dir is empty, ignore . and ..
bool check_empty (struct inode * inode)
{
  struct dir_entry e;
  off_t pos = 0;
  bool empty = true;

  while (inode_read_at (inode, &e, sizeof e, pos) == sizeof e) 
    {
      pos += sizeof e;
      if (e.in_use && strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0)
      {
        empty = false;
      }
    }
    return empty;
}