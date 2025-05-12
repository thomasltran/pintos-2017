#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

#define NAME_MAX 14  // Maximum filename length per spec
#define FD_MIN 2    // Reserve 0 (stdin), 1 (stdout), 2 (stderr)
#define FD_MAX 128 // maximum number of fds (per Dr Back reccomendation)

#define MUTEX_COUNT 1024
#define SEM_COUNT 1024
#define COND_COUNT 1024

void syscall_init (void);
void exit(int status); // declared here for use in exception.c
extern struct lock fs_lock; // global filesys lock

#endif /* userprog/syscall.h */
 