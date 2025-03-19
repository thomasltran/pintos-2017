#include <stdio.h>
#include <inttypes.h>
void init_st(void);
size_t st_write_at(void*, uint32_t);
void st_free_page(size_t);
void st_read_at(void* uaddr, uint32_t, size_t, bool);
