#include "frame.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <hash.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "swap.h"
#include "page.h"

static struct hash frame_table;		/* Frame Table. */
static struct lock frame_table_lock;	/* Lock for synchronizing access to frame table. */

/* Frame Table Allocator Functions. */

/* Hash function for the frame table.
   Returns hash value for the frame. */
unsigned
allocator_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame *f = hash_entry (e, struct frame, elem);
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

  // request all the pages from the user pool & insert them in frame table
  while (1)
  {
    // should you change the uint32_t to void ??
    void *kpage = palloc_get_page (PAL_USER);

    // no more pages in user pool
    if (!kpage)
	break;

    struct frame *f = (struct frame *) malloc (sizeof (struct frame));
    if (!f)
	PANIC ("Malloc cannot allocate memory for frame table\n");

    // initialize the frame
    f->kpage = kpage;
    f->upage = NULL;
    f->free = true;

    // hash_insert () should return NULL, as there should not be any
    // duplication of frame entry
    ASSERT (!hash_insert (&frame_table, &f->elem));
   // printf ("No of hash table entries %d\n", hash_size (&frame_table));
  }

//  hash_first (&clock_hand, &frame_table);
//  printf ("iterator's current element is %p\n", hash_cur (&clock_hand));
  lock_init (&frame_table_lock);
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
  hash_destroy (&frame_table, allocator_destroy_elem);
  lock_release (&frame_table_lock);
}

bool
is_frame_table_lock_acquired () {
  return lock_held_by_current_thread (&frame_table_lock);
}

void
release_frame_table_lock () {
  return lock_release (&frame_table_lock);
}

/* Allocates a free frame to the requesting thread.
   Currently panics the kernel if no free frame is available. */
/* Searches the frame table for a free frame, if found returns the kpage
   otherwise returns NULL. */
void *
allocator_get_free_page (void *upage)
{
  struct hash_iterator i;

  lock_acquire (&frame_table_lock); 
  hash_first (&i, &frame_table);

  while (hash_next (&i)) {
    struct frame *f = hash_entry (hash_cur (&i), struct frame, elem);
    if (f->free) {
      f->free = false;
      f->upage = upage;
      f->t = thread_current ();

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
//  ASSERT (kpage);
  struct frame f;
  struct hash_elem *e;

  lock_acquire (&frame_table_lock);

  f.kpage = kpage;
  e = hash_find (&frame_table, &f.elem);

  if (!e) {
    lock_release (&frame_table_lock);
    printf ("Incorrect kpage value given by thread %d\n",thread_current ()->tid);
    return;
  }

  struct frame *fte = hash_entry (e, struct frame, elem);
  fte->free = true;
  fte->upage = NULL;
  fte->t = NULL;

  lock_release (&frame_table_lock);
}

void allocator_dummy (void)
{
  printf ("Dummy called\n");
}

static struct frame* allocator_get_not_accessed_page (void);
static struct frame* allocator_clear_all_accessed_bits (void);
static void allocator_write_back_dirty_page (struct page *);

/* Search for a not accessed page.
   Need to acquire lock before accessing this function. */
static struct frame*
allocator_get_not_accessed_page () {
  // Need to evict a page
  // you cannot call hash_cur after calling hash_first but before hash_next
  struct hash_iterator j;
  hash_first (&j, &frame_table);
  while (hash_next (&j)) {
    struct frame *f = hash_entry (hash_cur (&j), struct frame, elem);

    // page is not accessed
    // should not be a stack page allocated by setup stack
    ASSERT (f->t);
    ASSERT (f->upage);

    bool accessed = pagedir_is_accessed (f->t->pagedir, f->upage);
    if (!accessed) {
      // found a not accessed page
//  printf ("Found a not accessed page %p by thread %d\n", f->upage,thread_current ()->tid);
      return f;
      break;
    }
  }

  return NULL;
}

static struct frame*
allocator_clear_all_accessed_bits ()
{
    struct hash_iterator i;
    hash_first (&i, &frame_table);

    while (hash_next (&i)) {
      struct frame *f = hash_entry (hash_cur (&i), struct frame, elem);
      ASSERT (f->t);
      ASSERT (f->upage);
// you are accessing other thread's data structure, which that thread may be changing
// leading to CHAOS
      pagedir_set_accessed (f->t->pagedir, f->upage, false);
    }

    hash_first (&i, &frame_table);
    hash_next (&i);
//    printf ("Cleared all the access bits\n");
    return hash_entry (hash_cur (&i), struct frame, elem);
}

static void
allocator_write_back_dirty_page (struct page *p)
{
//printf ("writing back a dirty page\n");
// may be this process is holding the lock
  bool release_lock = false;
  if (!lock_held_by_current_thread (&filesys_lock)) {
    release_lock = true;
    lock_acquire (&filesys_lock);
  }

  printf ("file name %s\n",p->file_name);
  struct file *fp = filesys_open (p->file_name);
  if (!fp) {
    printf ("Could not write back the changes to disk, thread tid %d file name %s \n", thread_current ()->tid, p->file_name);

    // if you acquired the lock here then release it
    if (release_lock)
      lock_release (&filesys_lock);

    exit_handler (-1);
  }

  file_seek (fp, p->file_ofs);
  if (file_write (fp, p->upage, p->read_bytes) != p->read_bytes) {
    printf ("Could not write back the changes to disk, thread tid %d file name %s \n", thread_current ()->tid, p->file_name);

    // if you acquired the lock here then release it
    if (release_lock)
      lock_release (&filesys_lock);
      exit_handler (-1);
  }

  // if you acquired the lock here then release it
  if (release_lock)
    lock_release (&filesys_lock);
}

static bool
allocator_insert_kpage_in_supplement_pt (void *upage, void *kpage, enum page_type_t page_type, bool writable)
{
	struct thread *t = thread_current ();
	ASSERT (kpage);
        lock_acquire (&t->supplement_lock);
        memset (kpage, 0, PGSIZE);

        /* Add the page to the process's address space. */
        if (!install_page (upage, kpage, writable))
        {
                allocator_free_page (kpage);
                lock_release (&t->supplement_lock);
                return false;
        }

        // adding the page entry to supplemental page table
        else {
		if (page_type == ALL_ZERO)
	                supplementary_insert_zero_page (upage, kpage);
		else if (page_type == IN_FILE || page_type == IN_SWAP || page_type == ALL_ZERO_I) {
			// cannot insert the kpage i.e. no upage exists
			if (!supplementary_insert_kpage (upage, kpage, page_type)) {
                		lock_release (&t->supplement_lock);
				return false;
			}
		}

		else {
			printf ("Unknown page type in allocator_insert_kpage_..\n");
                	lock_release (&t->supplement_lock);
			return false;
		}

                lock_release (&t->supplement_lock);
		return true;
        } 
}

/* Page eviction function. */
/* Just call the allocator_get_free_page () in order to find a free frame.
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
/* Now, upadating the frame table (upage too) with a single call. */
//void *
//allocator_get_page (void *upage) {
void *
allocator_get_page (void *upage, enum page_type_t page_type, bool writable) {
  // if free frames are available then allocate it
  void *kpage = allocator_get_free_page (upage);
  if (kpage) {
    if (!allocator_insert_kpage_in_supplement_pt (upage, kpage, page_type, writable)) {
      allocator_free_page (kpage);
      return NULL;
    }
    return kpage;
  }

//  printf ("No more free page available. Victimzing a page\n");

  struct frame *not_accessed = NULL;
  lock_acquire (&frame_table_lock);
//  printf ("Thread %d has acquired frame table lock\n", thread_current ()->tid);
  // now it's time to switch to array implementation of frame table
  // iterators are much of a headache in hash time for this static Table

  not_accessed = allocator_get_not_accessed_page ();
  if (!not_accessed) {
    not_accessed = allocator_clear_all_accessed_bits ();
  }

  // flush the page residing in the frame currently
//  printf ("Not accessed is %p & it's upage %p, thread is %d\n", not_accessed, not_accessed->upage, not_accessed->t->tid);
  ASSERT (not_accessed);
  ASSERT (!(not_accessed->free));
  ASSERT (not_accessed->t);
  ASSERT (not_accessed->upage);

  // do not allow other threads to change the supplement_pt till you 
  // are done with flushing the page
  lock_acquire (&not_accessed->t->supplement_lock);
  struct page *p = supplementary_lookup (not_accessed->t, not_accessed->upage);

  ASSERT (p);

  // if read-only page then just flush the page, as it can be re-read from the
  // disk on page fault
  if (p->writable) {
    if (p->page_type == IN_FILE) {
      // writable & page is dirty
/*    if (pagedir_is_dirty (not_accessed->t->pagedir, not_accessed->upage)) {
printf ("%d is writing back the changes to file\n", thread_current ()->tid);
        allocator_write_back_dirty_page (p);
      } */
    }

    else  if (p->page_type == ALL_ZERO || p->page_type == IN_MEMORY) {
      // write page to swap
//printf ("%d is Writing to swap sectors kpage %p of %d\n",thread_current ()->tid,p->kpage,not_accessed->t->tid);
      p->sector = swap_write (p->kpage);
ASSERT (p->sector != SECTOR_ERROR);
      p->page_type = IN_SWAP;
//printf ("%d Writing complete for kpage %p of %d\n",thread_current ()->tid, p->kpage,not_accessed->t->tid);
    }

    else {
      if (p->page_type == IN_SWAP)
        PANIC ("Page Type unknown, type is %s\n","IN_SWAP");
      else 
        PANIC ("Page Type unknown\n");
    }
  }

  p->kpage = NULL;
  // clear the page directory mapping for this page
  pagedir_clear_page (not_accessed->t->pagedir, p->upage);
  lock_release (&not_accessed->t->supplement_lock);
//printf ("Releasing lock %p of thread tid %d\n",&not_accessed->t->supplement_lock, not_accessed->t->tid);
// release the supplement_pt lock, to let other thread's modify the table

  not_accessed->free = false;
  not_accessed->upage = upage;
  not_accessed->t = thread_current ();

 // printf ("Thread %d is releasing frame table lock\n", thread_current ()->tid);
  lock_release (&frame_table_lock);

  if (!allocator_insert_kpage_in_supplement_pt (upage, not_accessed->kpage, page_type, writable)) {
      allocator_free_page (not_accessed->kpage);
      return NULL;
    }
  return not_accessed->kpage;
}

