#include "page.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

struct lock vm_lock;
static unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED);
static bool
page_less(const struct hash_elem *a_, const struct hash_elem *b_,
          void *aux UNUSED);

void init_spt()
{
    lock_init(&vm_lock);
}

struct supp_pt *create_supp_pt(void)
{
    struct supp_pt *supp_pt = malloc(sizeof(struct supp_pt));
    if (supp_pt == NULL)
    {
        return NULL;
    }
    hash_init(&supp_pt->hash_map, page_hash, page_less, NULL);
    return supp_pt;
}

struct page *create_page(void *uaddr, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_status)
{
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL)
    {
        return NULL;
    }
    page->uaddr = uaddr;
    page->file = file;
    page->ofs = ofs;
    page->read_bytes = read_bytes;
    page->zero_bytes = zero_bytes;
    page->writable = writable;
    return page;
}

struct page *find_page(struct supp_pt *supp_pt, void *uaddr)
{
    struct hash hash_map = supp_pt->hash_map;
    struct page page;
    page.uaddr = (void *)pg_no(uaddr);

    struct hash_elem *e = hash_find(&hash_map, &page.hash_elem);
    if(e == NULL){
        return NULL;
    }
    return hash_entry(e, struct page, hash_elem);
}

/* Returns a hash value for page p. */
static unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    return hash_bytes((void *)pg_no(p->uaddr), sizeof(void*));
}

/* Returns true if page a precedes page b. */
static bool
page_less(const struct hash_elem *a_, const struct hash_elem *b_,
          void *aux UNUSED)
{
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    return a->uaddr < b->uaddr;
}