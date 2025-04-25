#include <syscall.h>
#include "../syscall-nr.h"
#include <stddef.h>

/* Invokes syscall NUMBER, passing no arguments, and returns the
   return value as an `int'. */
#define syscall0(NUMBER)                                        \
        ({                                                      \
          int retval;                                           \
          asm volatile                                          \
            ("pushl %[number]; int $0x30; addl $4, %%esp"       \
               : "=a" (retval)                                  \
               : [number] "i" (NUMBER)                          \
               : "memory");                                     \
          retval;                                               \
        })

/* Invokes syscall NUMBER, passing argument ARG0, and returns the
   return value as an `int'. */
#define syscall1(NUMBER, ARG0)                                           \
        ({                                                               \
          int retval;                                                    \
          asm volatile                                                   \
            ("pushl %[arg0]; pushl %[number]; int $0x30; addl $8, %%esp" \
               : "=a" (retval)                                           \
               : [number] "i" (NUMBER),                                  \
                 [arg0] "g" (ARG0)                                       \
               : "memory");                                              \
          retval;                                                        \
        })

/* Invokes syscall NUMBER, passing arguments ARG0 and ARG1, and
   returns the return value as an `int'. */
#define syscall2(NUMBER, ARG0, ARG1)                            \
        ({                                                      \
          int retval;                                           \
          asm volatile                                          \
            ("pushl %[arg1]; pushl %[arg0]; "                   \
             "pushl %[number]; int $0x30; addl $12, %%esp"      \
               : "=a" (retval)                                  \
               : [number] "i" (NUMBER),                         \
                 [arg0] "r" (ARG0),                             \
                 [arg1] "r" (ARG1)                              \
               : "memory");                                     \
          retval;                                               \
        })

/* Invokes syscall NUMBER, passing arguments ARG0, ARG1, and
   ARG2, and returns the return value as an `int'. */
#define syscall3(NUMBER, ARG0, ARG1, ARG2)                      \
        ({                                                      \
          int retval;                                           \
          asm volatile                                          \
            ("pushl %[arg2]; pushl %[arg1]; pushl %[arg0]; "    \
             "pushl %[number]; int $0x30; addl $16, %%esp"      \
               : "=a" (retval)                                  \
               : [number] "i" (NUMBER),                         \
                 [arg0] "r" (ARG0),                             \
                 [arg1] "r" (ARG1),                             \
                 [arg2] "r" (ARG2)                              \
               : "memory");                                     \
          retval;                                               \
        })

void
halt (void) 
{
  syscall0 (SYS_HALT);
  NOT_REACHED ();
}

void
exit (int status)
{
  syscall1 (SYS_EXIT, status);
  NOT_REACHED ();
}

pid_t
exec (const char *file)
{
  return (pid_t) syscall1 (SYS_EXEC, file);
}

int
wait (pid_t pid)
{
  return syscall1 (SYS_WAIT, pid);
}

bool
create (const char *file, unsigned initial_size)
{
  return syscall2 (SYS_CREATE, file, initial_size);
}

bool
remove (const char *file)
{
  return syscall1 (SYS_REMOVE, file);
}

int
open (const char *file)
{
  return syscall1 (SYS_OPEN, file);
}

int
filesize (int fd) 
{
  return syscall1 (SYS_FILESIZE, fd);
}

int
read (int fd, void *buffer, unsigned size)
{
  return syscall3 (SYS_READ, fd, buffer, size);
}

int
write (int fd, const void *buffer, unsigned size)
{
  return syscall3 (SYS_WRITE, fd, buffer, size);
}

void
seek (int fd, unsigned position) 
{
  syscall2 (SYS_SEEK, fd, position);
}

unsigned
tell (int fd) 
{
  return syscall1 (SYS_TELL, fd);
}

void
close (int fd)
{
  syscall1 (SYS_CLOSE, fd);
}

mapid_t
mmap (int fd, void *addr)
{
  return syscall2 (SYS_MMAP, fd, addr);
}

void
munmap (mapid_t mapid)
{
  syscall1 (SYS_MUNMAP, mapid);
}

bool
chdir (const char *dir)
{
  return syscall1 (SYS_CHDIR, dir);
}

bool
mkdir (const char *dir)
{
  return syscall1 (SYS_MKDIR, dir);
}

bool
readdir (int fd, char name[READDIR_MAX_LEN + 1]) 
{
  return syscall2 (SYS_READDIR, fd, name);
}

bool
isdir (int fd) 
{
  return syscall1 (SYS_ISDIR, fd);
}

int
inumber (int fd) 
{
  return syscall1 (SYS_INUMBER, fd);
}

void twrapper(void *userfun_ptr, void *userarg)
{
  userfun_t userfun = (userfun_t)userfun_ptr;
  void * res = userfun(userarg);
  pthread_exit(res);
}

int pthread_create(void (*wrapper)(void *, void *), void *userfun, void *userarg)
{
  return syscall3(SYS_PTHREAD_CREATE, wrapper, userfun, userarg);
}

int _pthread_create(userfun_t userfun, void *arg)
{
  return pthread_create(twrapper, (void *)userfun, arg);
}

int pthread_join(int tid, void ** res){
  return syscall2(SYS_PTHREAD_JOIN, tid, res);
}

void pthread_exit(void * res){
  syscall1(SYS_PTHREAD_EXIT, res);
}

int pthread_mutex_init(pthread_mutex_t * pthread_mutex)
{
  return syscall1(SYS_MUTEX_INIT, pthread_mutex);
}

int pthread_mutex_lock(pthread_mutex_t * pthread_mutex)
{
  return syscall1(SYS_MUTEX_LOCK, pthread_mutex);
}

int pthread_mutex_unlock(pthread_mutex_t * pthread_mutex)
{
  return syscall1(SYS_MUTEX_UNLOCK, pthread_mutex);
}

int pthread_mutex_destroy(pthread_mutex_t * pthread_mutex)
{
  return syscall1(SYS_MUTEX_DESTROY, pthread_mutex);
}

int sem_init(sem_t * sem, unsigned int val)
{
  return syscall2(SYS_SEM_INIT, sem, val);
}

int sem_post(sem_t * sem)
{
  return syscall1(SYS_SEM_POST, sem);
}

int sem_down(sem_t * sem)
{
  return syscall1(SYS_SEM_DOWN, sem);
}

int sem_destroy(sem_t * sem)
{
  return syscall1(SYS_SEM_DESTROY, sem);
}

int pthread_cond_init(pthread_cond_t * cond)
{
  return syscall1(SYS_COND_INIT, cond);
}

int pthread_cond_signal(pthread_cond_t * cond, pthread_mutex_t * pthread_mutex)
{
  return syscall2(SYS_COND_SIGNAL, cond, pthread_mutex);
}

int pthread_cond_broadcast(pthread_cond_t * cond, pthread_mutex_t * pthread_mutex)
{
  return syscall2(SYS_COND_BROADCAST, cond, pthread_mutex);
}

int pthread_cond_wait(pthread_cond_t * cond, pthread_mutex_t * pthread_mutex)
{
  return syscall2(SYS_COND_WAIT, cond, pthread_mutex);
}

int pthread_cond_destroy(pthread_cond_t * cond)
{
  return syscall1(SYS_COND_DESTROY, cond);
}