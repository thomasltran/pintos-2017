typedef void *(*userfun_t)(void *);

typedef struct {
    int pthread_mutex_id;
} pthread_mutex_t;

typedef struct
{
    int sem_id;
} sem_t;

typedef struct
{
    int cond_id;
} pthread_cond_t;

typedef struct
{
    void * data[0]; // 0 for pthread_self, 1-> for everything else
} tls;

#define PTHREAD_SIZE (8 * 1024 * 1024)
#define PTHREAD_SPACE (PTHREAD_SIZE * 32)
#define PTHREAD_REGION (PHYS_BASE - PTHREAD_SPACE) // bottom of the range, prob can assert to check in create
#define TLS_SIZE (1024 * 4)

// from running_thread
static inline void *curr_esp(void)
{
    void *esp;
    asm("mov %%esp, %0" : "=g"(esp));
    return esp;
}

// rounds to bottom of 8 MB chunk
static inline tls *get_tls_ptr(void)
{
    return (tls *)((void *)((uintptr_t)curr_esp() & ~(PTHREAD_SIZE - 1)));
}

// change to an offset with the array approach
#define get_tls(field) (get_tls_ptr()->data[field])
#define set_tls(field, value) (get_tls_ptr()->data[field] = (value))