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

#include "devices/disk.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"

static struct hash frame_table;		/* Frame Table. */
static struct lock frame_table_lock;	/* Lock for synchronizing access to frame table. */

static struct hash_iterator clock_hand;	/* Clock pointer to the frame table entry. */


#define SLOT_SIZE (PGSIZE / DISK_SECTOR_SIZE)
static struct lock swap_table_lock;
static struct bitmap *swap_table;
static int num_swap_slots;

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
    //f->pte = NULL;
    f->upage = NULL;
    f->free = true;

    // hash_insert () should return NULL, as there should not be any
    // duplication of frame entry
    ASSERT (!hash_insert (&frame_table, &f->elem));
   // printf ("No of hash table entries %d\n", hash_size (&frame_table));
  }

  hash_first (&clock_hand, &frame_table);
  printf ("iterator's current element is %p\n", hash_cur (&clock_hand));
  lock_release (&frame_table_lock);
}

void
allocator_destroy_elem (struct hash_elem *e,void *aux UNUSED)
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
/* Searches the frame table for a free frame, if found returns the kpage
   otherwise returns NULL. */
void *
allocator_get_free_page ()
{
  lock_acquire (&frame_table_lock);

  struct hash_iterator i;
  hash_first (&i, &frame_table);

  while (hash_next (&i)) {
    struct frame *f = hash_entry (hash_cur (&i), struct frame, elem);
    if (f->free) {
	f->free = false;
//	f->pte = NULL;
	f->upage = NULL;
	f->t = NULL;
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

  if (!e) {
    lock_release (&frame_table_lock);
 //   printf ("Incorrect kpage value given\n");
    return;
  }

  struct frame *fte = hash_entry (e, struct frame, elem);
  fte->free = true;
//  fte->pte = NULL;
  fte->upage = NULL;
  fte->t = NULL;

  lock_release (&frame_table_lock);
}

// initializes the frame's pte
bool
//allocator_insert_pte (void *kpage, void *pte)
allocator_insert_upage (void *kpage, void *upage)
{
  struct frame f;
  f.kpage = kpage;

  lock_acquire (&frame_table_lock);
  struct hash_elem *e = hash_find (&frame_table, &f.elem);

  if (!e) {
    lock_release (&frame_table_lock);
//    printf ("Incorrect kpage value given\n");
    return false;
  }

  struct frame *fp = hash_entry (e, struct frame, elem);
  //fp->pte = pte;
  fp->upage = upage;
  fp->t = thread_current ();

  lock_release (&frame_table_lock);
  return true;
}


void
re_init_frame (struct frame *f)
{
  f->free = false;
  f->t = NULL;
  f->upage = NULL;
}

/* Page eviction function. */

/* Just call the allocator_get_page () in order to find a free frame.
   If no free frame found, then search for a frame with accessed bit 0,
   if no such page found, clear the accessed bit for all the frames &
   free the first frame.
   This way you get a frame to allocate, if this frame has some page initially,
   then if page was read-only, just flush it, on page fault you need to read 
   it back,
   if page was ALL_ZERO & it was accessed before, you write it to swap & remember
   the swap slot where it is written.
   if page was ALL_ZERO & it was not accessed just flush it.
   Be sure about clearing the accessed bits only after you make the distinction 
   between accessed & not accessed. */

void allocator_dummy (void)
{
 // printf ("Dummy called\n");
}

void *
allocator_get_page () {
  // if free frames are available then allocate it
  void *kpage = allocator_get_free_page ();
  if (kpage) {
    return kpage;
  }

//  printf ("No more free page available. Victimzing a page\n");
  //allocator_dummy ();

  lock_acquire (&frame_table_lock);

  // Need to evict a page
  // you cannot call hash_cur after calling hash_first but before hash_next
  //struct hash_elem *cur = hash_cur (&clock_hand);

  // now it's time to switch to array implementation of frame table
  // iterators are much of a headache in hash time for this static Table
  hash_first (&clock_hand, &frame_table);
  hash_next (&clock_hand);

  struct hash_elem *cur = hash_cur (&clock_hand);
//  struct hash_elem *cur = clock_hand;
  struct frame *not_accessed = NULL;

  struct frame *initial_f = hash_entry (cur, struct frame, elem);
  if (!pagedir_is_accessed (thread_current ()->pagedir, initial_f->upage)) {
    printf ("Found the page at clock_hand itself %p\n",cur);
    not_accessed = initial_f;
  }

  else {
  // search for a not accessed page
  //while (hash_next (&clock_hand) != cur) {
  bool found = false;
  while (hash_next (&clock_hand)) {
/*    if (hash_cur (&clock_hand) == NULL) {
printf ("Hash cur is NULL\n");
      hash_first (&clock_hand, &frame_table);
    }
*/
    struct frame *f = hash_entry (hash_cur (&clock_hand), struct frame, elem);

    // page is not accessed
    if (!pagedir_is_accessed (thread_current ()->pagedir, f->upage)) {
      // found a not accessed page
//printf ("Found a not accessed page %p hash_cur (&clock_hand) %p\n", f->upage, hash_cur (&clock_hand));
      not_accessed = f;
      found = true;
      break;
    }
  }

  // if we have travelled the whole table, w/o finding an unaccessed page
// above comment is not valid, 
  //if (hash_cur (&clock_hand) == cur) {
  //if (hash_cur (&clock_hand)) {
  if (!found) {
    struct hash_iterator i;
    hash_first (&i, &frame_table);

    while (hash_next (&i)) {
      struct frame *f = hash_entry (hash_cur (&i), struct frame, elem);
      ASSERT (f->t);
      //ASSERT (is_thread (f->t));

      //ASSERT (lookup_page (f->t->pagedir, f->upage, false));
      ASSERT (pagedir_search_page (f->t->pagedir, f->upage));
      pagedir_set_accessed (f->t->pagedir, f->upage, false);
    }

    //not_accessed = hash_entry (hash_cur (&clock_hand), struct frame, elem);
    hash_first (&i, &frame_table);
    hash_next (&i);
    not_accessed = hash_entry (hash_cur (&i), struct frame, elem);
  }
  } // end of if (initial frame is not accessed)

  // flush the page residing in the frame currently
  ASSERT (!(not_accessed->free));
  ASSERT (not_accessed->t);
  ASSERT (not_accessed->upage);
//  printf ("Not accessed is %p & it's upage %p, thread is %s\n", not_accessed, not_accessed->upage, not_accessed->t->name);

  struct page *p = supplementary_lookup (not_accessed->t, not_accessed->upage);

  ASSERT (p);

  // if read-only page then just flush the page, as it can be re-read from the
  // disk on page fault

  if (p->writable) {
    if (p->page_type == IN_FILE) {
      // writable & page is dirty
      if (pagedir_is_dirty (not_accessed->t->pagedir, not_accessed->upage)) {
 // this process should not be acquiring this lock
printf ("writing back a dirty page\n");
	lock_acquire (&filesys_lock);
	struct file *fp = filesys_open (p->file_name);
        if (!fp) {
	  printf ("Could not write back the changes to disk\n");
	  lock_release (&filesys_lock);
	  exit_handler (-1);
	}

	file_seek (fp, p->file_ofs);
	if (file_write (fp, p->upage, p->read_bytes) != p->read_bytes) {
	  printf ("Could not write back the changes to disk\n");
	  lock_release (&filesys_lock);
	  exit_handler (-1);
	}

	lock_release (&filesys_lock);
      } 
    }

    else  if (p->page_type == ALL_ZERO || p->page_type == IN_MEMORY) {
      // write page to swap
      p->sector = swap_write (p->upage);
ASSERT (p->sector != SECTOR_ERROR);
      p->page_type = IN_SWAP;
    }

    else {
      PANIC ("Page Type unknown\n");
    }

    // clear the page directory mapping for this page
    p->kpage = NULL;
    p->is_present = false;
    pagedir_clear_page (not_accessed->t->pagedir, p->upage);
  }

  re_init_frame (not_accessed);
  lock_release (&frame_table_lock);
  return not_accessed->kpage;
}

/* Swap Table access methods. */
void swap_init () 
{
  struct disk *swap = disk_get (1,1);	// swap 1:1 channel:device no
  if (!swap)
    PANIC ("No swap disk found\n");

  disk_sector_t size = disk_size (swap);

  num_swap_slots = (size * DISK_SECTOR_SIZE ) / PGSIZE;

  swap_table = bitmap_create (size);
  if (!swap_table)
    PANIC ("Cannot create bitmap for the swap table\n");

  lock_init (&swap_table_lock);
}

// returns the start sector of the swap slot where upage is written
// all the vacant swap slots are set to FALSE
size_t
swap_write (void *upage)
{
//  printf ("Swap write called for %p\n",upage);
  lock_acquire (&swap_table_lock);

  size_t idx = bitmap_scan_and_flip (swap_table, 0, SLOT_SIZE, false);
  if (idx == BITMAP_ERROR) {
    PANIC ("No more swap slot avaiable\n");
  }

  struct disk *swap = disk_get (1,1);
  if (!swap)
    PANIC ("No swap disk found\n");

  int offset;
  for (offset = 0; offset < SLOT_SIZE; offset++) {
    disk_write (swap, idx + offset, upage + (offset * DISK_SECTOR_SIZE));
  }

  lock_release (&swap_table_lock);
  return idx;
}

// all the used swap slots are set to TRUE
void
swap_read_and_free (disk_sector_t idx, void *upage)
//swap_read_and_free (int idx, void *upage)
{
  lock_acquire (&swap_table_lock);

  // all the swap sectors must be in use
  // bitmap_all returns true if all pages are in use (i.e. TRUE)
  // take care of the edge cases
  ASSERT (idx != SECTOR_ERROR);
  ASSERT (bitmap_all (swap_table, idx, SLOT_SIZE));

  struct disk *swap = disk_get (1,1);
  if (!swap)
    PANIC ("No swap disk found\n");

  // read in the upage buffer
  int offset;
  for (offset = 0; offset < SLOT_SIZE; offset++) {
    printf ("Swap read sector %d at address %p\n",idx+offset, upage +(offset * DISK_SECTOR_SIZE));
    disk_read (swap, idx + offset, upage + (offset * DISK_SECTOR_SIZE));
  }

  // vacant the swap slot
  bitmap_set_multiple (swap_table, idx, SLOT_SIZE, false);

  lock_release (&swap_table_lock);
  printf ("Swap read called at %d for %p\n",idx,upage);
}

void swap_exit ()
{
  lock_acquire (&swap_table_lock);
  bitmap_destroy (swap_table);
  lock_release (&swap_table_lock);
}

/* Supplementary Page Table's access methods.*/

/* Hash function for the frame table.
   Returns hash value for the frame. */
unsigned
supplementary_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, elem);
  //return hash_bytes (&p->pte, sizeof p->pte);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns True if frame a precedes frame b. */
bool
supplementary_less (const struct hash_elem *a_, const struct hash_elem *b_,
			void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);

  //return a->pte < b->pte;
  return a->upage < b->upage;
}

void
supplementary_init (struct hash *spt)
{
  hash_init (spt, supplementary_hash, supplementary_less, NULL);
}

//bool
//supplementary_insert (uint32_t *pte, void *kpage, const char *file_name,
void
supplementary_insert (void *upage, const char *file_name,
			int32_t ofs,uint32_t read_bytes, bool writable, enum page_type_t type)
{
  // if the page entry is available in supplementary page table
  // currently ignore the given parameters & return
  if (supplementary_lookup (thread_current (), upage))
    return;

  struct page *p = (struct page *) malloc (sizeof (struct page));
  if (!p)
    PANIC ("Cannot allocate memory to supplementary page table entry\n");

  p->upage = upage;
  p->is_present = false;  // no kpage is allotted to it
  p->kpage = NULL;
  //p->sector = -1;	// invalid sector
  p->sector = SECTOR_ERROR;	// invalid sector
  strlcpy (p->file_name, file_name, sizeof p->file_name);
  p->file_ofs = ofs;
  p->page_type = type;
  p->read_bytes = read_bytes;
  p->writable = writable;

  hash_insert (&thread_current ()->supplement_pt, &p->elem);

/*  if (type == ALL_ZERO)
	printf ("Inserted SPT: %p n Type is %s\n", upage,"all_zero");
  else if (type == IN_FILE)
	printf ("Inserted SPT: %p n Type is %s\n", upage,"in_file"); */
}

void
supplementary_destroy_elem (struct hash_elem *e,void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, elem);
  allocator_free_page (p->kpage);
  free (p);
}

void
supplementary_exit ()
{
  hash_destroy (&thread_current ()->supplement_pt, supplementary_destroy_elem);
}

struct page *
supplementary_lookup (struct thread *t, void *upage)
{
//  struct thread *cur = thread_current ();
  struct page _p;

//  uint32_t *pte = pagedir_search_page (&cur->pagedir, upage);

//  if (pte)
//    _p.pte = pte;

  _p.upage = upage;

/*  else {
    printf ("NO such upage is mapped to the thread's page table\n");
    return NULL;
  }*/

  // lookup for page with pte as the above one
  //struct hash_elem *e = hash_find (&cur->supplement_pt, &_p.elem);
  struct hash_elem *e = hash_find (&t->supplement_pt, &_p.elem);

  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

bool
supplementary_insert_kpage (void *upage, void *kpage)
{
//  struct page *p = supplementary_lookup (upage);
  struct page *p = supplementary_lookup (thread_current (), upage);

  if (p) {
    p->kpage = kpage;
    p->is_present = true; // kpage is allotted to it
    return true;
  }

  printf ("No such upage exists in supplementary page table\n");
  return false;
}
