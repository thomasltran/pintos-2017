#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

#define NAME_MAX 14  // Maximum filename length per spec
#define FD_MIN 2    // Reserve 0 (stdin), 1 (stdout), 2 (stderr)
#define FD_MAX 128 // maximum number of fds (per Dr Back reccomendation)

void syscall_init (void);
extern struct lock fs_lock;

#endif /* userprog/syscall.h */
