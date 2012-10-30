/* Frame Table Allocator Functions. */

/* Initializes the frame table by calling
   palloc_get_page (PAL_USER) till it returns NULL.
   Allocates memory via malloc for holding info
   about the frames for each successful call.
   Also initializes the lock. */
void
allocator_init (void)
{
  lock_init (&frame_table_lock);

  lock_acquire (&frame_table_lock);

  // request all the pages from the user pool & insert them in frame table
  while (1)
  {
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
  printf ("Dummy called\n");
}

void *
allocator_get_page () {
  // if free frames are available then allocate it
  void *kpage = allocator_get_free_page ();
  if (kpage) {
    return kpage;
  }

 // printf ("No more free page available. Victimzing a page\n");
 // allocator_dummy ();

  lock_acquire (&frame_table_lock);

  // Need to evict a page
  // you cannot call hash_cur after calling hash_first but before hash_next
  //struct hash_elem *cur = hash_cur (&clock_hand);

  // now it's time to switch to array implementation of frame table
  // iterators are much of a headache in hash time for this static Table
  hash_first (&clock_hand, &frame_table);
  hash_next (&clock_hand);

  struct hash_elem *cur = hash_cur (&clock_hand);
  struct frame *not_accessed = NULL;

  struct frame *initial_f = hash_entry (cur, struct frame, elem);
  uint8_t *base_esp = (uint8_t *) PHYS_BASE - PGSIZE;
  bool found = false;

  // should not be a stack page allocated by setup stack
//printf ("initial_f upage %p & kpage %p & phys_base - pgsize is %p\n", initial_f->upage, initial_f->kpage,base_esp);
  if (initial_f->upage != base_esp) {
    if (!pagedir_is_accessed (thread_current ()->pagedir, initial_f->upage)) {
      printf ("Found the page at clock_hand itself %p\n",cur);
      not_accessed = initial_f;
      found = true;
    }
  }

  //else {
  if (!found) {
  // search for a not accessed page
  //while (hash_next (&clock_hand) != cur) {
  while (hash_next (&clock_hand)) {
/*    if (hash_cur (&clock_hand) == NULL) {
printf ("Hash cur is NULL\n");
      hash_first (&clock_hand, &frame_table);
    }
*/
    struct frame *f = hash_entry (hash_cur (&clock_hand), struct frame, elem);

    // page is not accessed
    // should not be a stack page allocated by setup stack
    if (f->upage != base_esp) {
      if (!pagedir_is_accessed (thread_current ()->pagedir, f->upage)) {
      // found a not accessed page
//printf ("Found a not accessed page %p hash_cur (&clock_hand) %p\n", f->upage, hash_cur (&clock_hand));
        not_accessed = f;
        found = true;
        break;
      }
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

    while (hash_next (&i)) {
      struct frame *f = hash_entry (hash_cur (&i), struct frame, elem);
      
      if (f->upage != base_esp) {
        not_accessed = f;
	found = true;
      }

      ASSERT (pagedir_search_page (f->t->pagedir, f->upage));
    }

    if (!found)
	PANIC ("Cannot find a frame to evict.. all base stack upages\n");
  }
  } // end of if (initial frame is not accessed)

  // flush the page residing in the frame currently
  ASSERT (!(not_accessed->free));
  ASSERT (not_accessed->t);
  ASSERT (not_accessed->upage);
  ASSERT (not_accessed->upage != base_esp);
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
