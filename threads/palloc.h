#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>

/* Frame Table is implemented in the form of a hash table. */
#include <hash.h>

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero page contents. */
    PAL_USER = 004              /* User page. */
  };

/* Maximum number of pages to put in user pool. */
extern size_t user_page_limit;

void palloc_init (void);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);


/* Frame Table Structure & it's access methods. */

// Frame Table Entry structure
struct frame
{
  void *kpage;		/* Kernel Virtual address of physical frame. */
  void *pte;		/* Page table entry of the page residing at this frame. */
  bool free;			/* Is frame free? */
  struct hash_elem elem;	/* Hash element to be embedded in hash table. */
};

static struct hash frame_table;		/* Frame Table. */
static struct lock frame_table_lock;	/* Lock for synchronizing access to frame table. */

void allocator_init (void);
void *allocator_get_page ();
void allocator_free_page (void *);
void allocator_exit ();
void allocator_destroy_elem (struct hash_elem *,void *);

unsigned allocator_hash (const struct hash_elem *e, void *);
bool allocator_less (const struct hash_elem *, const struct hash_elem *,void *);

#endif /* threads/palloc.h */
