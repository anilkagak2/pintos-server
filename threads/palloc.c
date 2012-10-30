#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct bitmap *used_map;            /* Bitmap of free pages. */
    uint8_t *base;                      /* Base of pool. */
  };

/* Two pools: one for kernel data, one for user pages. */
struct pool kernel_pool, user_pool;

/* Maximum number of pages to put in user pool. */
size_t user_page_limit = SIZE_MAX;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

/* Initializes the page allocator. */
void
palloc_init (void) 
{
  /* End of the kernel as recorded by the linker.
     See kernel.lds.S. */
  extern char _end;

  /* Free memory. */
  uint8_t *free_start = pg_round_up (&_end);
  uint8_t *free_end = ptov (ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;

  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }

  return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;

  printf ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);

  return page_no >= start_page && page_no < end_page;
}

/* Frame Table Allocator Functions. */

/* Hash function for the frame table.
   Returns hash value for the frame. */
unsigned
allocator_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame *f = hash_entry (e, struct frame, elem);
  //return hash_bytes (&f->kpage, sizeof f->kpage);
  return hash_bytes (&f->kpage, sizeof f->kpage);
}

/* Returns True if frame a precedes frame b. */
bool
allocator_less (const struct hash_elem *a_, const struct hash_elem *b_,
			void *aux UNUSED)
{
  const struct frame *a = hash_entry (a_, struct frame, elem);
  const struct frame *b = hash_entry (b_, struct frame, elem);

  return a->kpage < b->kpage;
}

/* Initializes the frame table by calling
   palloc_get_page (PAL_USER) till it returns NULL.
   Allocates memory via malloc for holding info
   about the frames for each successful call.
   Also initializes the lock. */
void
allocator_init (void)
{
  hash_init (&frame_table, allocator_hash, allocator_less, NULL);
  lock_init (&frame_table_lock);

  lock_acquire (&frame_table_lock);

  // request all the pages from the user pool & insert them in frame table
  while (1)
  {
    // should you change the uint32_t to void ??
    //uint32_t *kpage = (uint32_t *)palloc_get_page (PAL_USER);
    void *kpage = palloc_get_page (PAL_USER);

    // no more pages in user pool
    if (!kpage)
	break;

    struct frame *f = (struct frame *) malloc (sizeof (struct frame));
    if (!f)
	PANIC ("Malloc cannot allocate memory for frame table\n");

    // initialize the frame
    f->kpage = kpage;
    f->pte = NULL;
    f->free = true;

    // hash_insert () should return NULL, as there should not be any
    // duplication of frame entry
    ASSERT (!hash_insert (&frame_table, &f->elem));
    printf ("No of hash table entries %d\n", hash_size (&frame_table));
  }

  lock_release (&frame_table_lock);
}

void
allocator_destroy_elem (struct hash_elem *e,void *aux)
{
  struct frame *f = hash_entry (e, struct frame, elem);
  free (f);
}

void
allocator_exit ()
{
  lock_acquire (&frame_table_lock);
  //hash_clear (&frame_table, allocator_destroy_elem);
  hash_destroy (&frame_table, allocator_destroy_elem);
  lock_release (&frame_table_lock);
}

/* Allocates a free frame to the requesting thread.
   Currently panics the kernel if no free frame is available. */
void *
allocator_get_page ()
{
  lock_acquire (&frame_table_lock);

  struct hash_iterator i;
  hash_first (&i, &frame_table);

  while (hash_next (&i)) {
    struct frame *f = hash_entry (hash_cur (&i), struct frame, elem);
    if (f->free) {
	f->free = false;
  	lock_release (&frame_table_lock);
	return f->kpage;
    }
  }

  lock_release (&frame_table_lock);
  return NULL;
}

/* Frees a frame by enabling it's free bit in frame table. */
void
allocator_free_page (void *kpage)
{
  lock_acquire (&frame_table_lock);

  struct frame f;
  struct hash_elem *e;

  f.kpage = kpage;

  e = hash_find (&frame_table, &f.elem);
  struct frame *fte = hash_entry (e, struct frame, elem);
  fte->free = true;

  lock_release (&frame_table_lock);
}



