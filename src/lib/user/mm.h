#include <stdio.h>
#include <syscall.h>

extern int mm_init(void * start, size_t len);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);

extern void * _mm_malloc(size_t size, pthread_mutex_t lock);
extern void _mm_free (void *ptr, pthread_mutex_t lock);


/* 
 * Students work in teams of one or two.  Teams enter their team name, 
 * personal names and login IDs in a struct of this
 * type in their mm.c file.
 */
typedef struct {
    char *teamname; /* ID1+ID2 or ID1 */
    char *name1;    /* full name of first member */
    char *id1;      /* login ID of first member */
    char *name2;    /* full name of second member (if any) */
    char *id2;      /* login ID of second member */
} team_t;

extern team_t team;

