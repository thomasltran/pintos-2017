#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

#define NAME_MAX 14  // Maximum filename length per spec
#define FD_MIN 2    // Reserve 0 (stdin), 1 (stdout), 2 (stderr)
#define FD_MAX 128 // maximum number of fds (per Dr Back reccomendation)

void syscall_init (void);
void exit(int status); // declared here for use in exception.c
extern struct lock fs_lock; // global filesys lock
#ifdef VM
bool get_pinned_frames(void *uaddr, bool write, size_t size);
void unpin_frames(void *uaddr, size_t size);
#endif

#endif /* userprog/syscall.h */
