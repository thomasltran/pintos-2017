#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

#define NAME_MAX 14  // Maximum filename length per spec
#define FD_MIN 2    // Reserve 0 (stdin), 1 (stdout), 2 (stderr)
#define FD_MAX 128 // maximum number of fds (per Dr Back reccomendation)

#define MUTEX_COUNT 64

#define PTHREAD_SIZE (8 * 1024 * 1024)
#define PTHREAD_SPACE (PTHREAD_SIZE * 32)
#define PTHREAD_REGION (PHYS_BASE - PTHREAD_SPACE) // bottom of the range

void syscall_init (void);
void exit(int status); // declared here for use in exception.c
extern struct lock fs_lock; // global filesys lock

#endif /* userprog/syscall.h */
