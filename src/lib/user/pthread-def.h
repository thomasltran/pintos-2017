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
    int pthread_id;
    void * data; // [0]
} tls;
// array of void * 
#define PTHREAD_SIZE (8 * 1024 * 1024)
#define PTHREAD_SPACE (PTHREAD_SIZE * 32)
#define PTHREAD_REGION (PHYS_BASE - PTHREAD_SPACE) // bottom of the range, prob can assert to check in create
#define TLS_SIZE (1024 * 4)

static inline void *curr_esp(void)
{
    void *esp;
    asm("mov %%esp, %0" : "=g"(esp));
    return esp;
}

static inline tls *get_tls_ptr(void)
{
    return (tls *)((void *)((uintptr_t)curr_esp() & ~(PTHREAD_SIZE - 1)));
}

// change to an offset with the array approach
// #define get_tls(field) (get_tls_ptr()->data[field])
#define get_tls(field) (get_tls_ptr()->field)
#define set_tls(field, value) (get_tls_ptr()->field = (value))