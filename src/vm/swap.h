#include <stdio.h>
#include <inttypes.h>

void init_st(void);
size_t st_write_at(void* uaddr);
void st_free_page(size_t);
void st_read_at(void* uaddr, size_t id);
