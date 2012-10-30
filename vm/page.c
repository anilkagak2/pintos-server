#include "page.h"
#include "frame.h"
#include "swap.h"
#include "threads/palloc.h"
#include <hash.h>
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <string.h>

/* Supplementary Page Table's access methods.*/

/* Hash function for the frame table.
   Returns hash value for the frame. */
unsigned
supplementary_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns True if frame a precedes frame b. */
bool
supplementary_less (const struct hash_elem *a_, const struct hash_elem *b_,
			void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);

  return a->upage < b->upage;
}

// you should change this function to take void args
// & initialize the thread_current()'s spt & spt_lock
void
supplementary_init (struct hash *spt)
{
  hash_init (spt, supplementary_hash, supplementary_less, NULL);
  lock_init (&thread_current ()->supplement_lock);
}

void
supplementary_insert (void *upage, const char *file_name,
			int32_t ofs,uint32_t read_bytes, bool writable, enum page_type_t type)
{
  struct thread *t = thread_current ();
  // if the page entry is available in supplementary page table
  // currently ignore the given parameters & return
  lock_acquire (&t->supplement_lock);

  if (supplementary_lookup (t, upage)) {
    lock_release (&t->supplement_lock);
    return;
  }

  struct page *p = (struct page *) malloc (sizeof (struct page));
  if (!p)
    PANIC ("Cannot allocate memory to supplementary page table entry\n");

  p->upage = upage;
  p->kpage = NULL;
  p->sector = SECTOR_ERROR;	// invalid sector
  strlcpy (p->file_name, file_name, sizeof p->file_name);
  p->file_ofs = ofs;
  p->page_type = type;
  p->read_bytes = read_bytes;
  p->writable = writable;

  hash_insert (&t->supplement_pt, &p->elem);
  lock_release (&t->supplement_lock);

/*  if (type == ALL_ZERO)
	printf ("Inserted SPT: %p n Type is %s\n", upage,"all_zero");
  else if (type == IN_FILE)
	printf ("Inserted SPT: %p n Type is %s\n", upage,"in_file"); */
}

void
supplementary_insert_without_lock (void *upage, const char *file_name,
			int32_t ofs,uint32_t read_bytes, bool writable, enum page_type_t type)
{
  struct thread *t = thread_current ();
  // if the page entry is available in supplementary page table
  // currently ignore the given parameters & return

  if (supplementary_lookup (t, upage)) {
    return;
  }

  struct page *p = (struct page *) malloc (sizeof (struct page));
  if (!p)
    PANIC ("Cannot allocate memory to supplementary page table entry\n");

//printf ("Thread %d inserting upage %p  at kpage NULL page type %d & writable %d\n",t->tid, upage, type,writable);
  p->upage = upage;
  p->kpage = NULL;
  p->sector = SECTOR_ERROR;	// invalid sector
  strlcpy (p->file_name, file_name, sizeof p->file_name);
  p->file_ofs = ofs;
  p->page_type = type;
  p->read_bytes = read_bytes;
  p->writable = writable;

  hash_insert (&t->supplement_pt, &p->elem);
}

void
supplementary_insert_zero_page (void *upage, void *kpage)
{
	struct thread *t = thread_current ();
	struct page *p = supplementary_lookup (t, upage);

// should not come to this case
	if (p) {
printf ("Thread %d: upage %p exists at kpage %p page type %d & writable %d\n",t->tid, upage, p->kpage, p->page_type,p->writable);
//	  ASSERT (p->kpage == kpage);
	  return;
	}

	p = (struct page *) malloc (sizeof (struct page));
	if (!p)
	  PANIC ("Cannot allocate memory to supplementary page table entry\n");

	p->upage = upage;
	p->kpage = kpage;
	p->sector = SECTOR_ERROR;     // invalid sector
	strlcpy (p->file_name, "" , sizeof p->file_name);
	p->file_ofs = 0;
	p->page_type = ALL_ZERO;
	p->read_bytes = 0;
	p->writable = true;

	hash_insert (&t->supplement_pt, &p->elem);
}

void
supplementary_destroy_elem (struct hash_elem *e,void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, elem);

  if (p->kpage)
  allocator_free_page (p->kpage);

  free (p);
}

void
supplementary_exit ()
{
  struct thread *t = thread_current ();
  lock_acquire (&t->supplement_lock);
  hash_destroy (&t->supplement_pt, supplementary_destroy_elem);
  lock_release (&t->supplement_lock);
}

// call this function with the supplement_lock acquired
struct page *
supplementary_lookup (struct thread *t, void *upage)
{
  struct page _p;
  _p.upage = upage;

  // lookup for page with pte as the above one
  struct hash_elem *e = hash_find (&t->supplement_pt, &_p.elem);

  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

bool
supplementary_insert_kpage (void *upage, void *kpage, enum page_type_t type)
{
  struct thread *t = thread_current ();
  struct page *p = supplementary_lookup (t, upage);

  if (p) {
    p->kpage = kpage;

    if (type == IN_SWAP) {
      swap_read_and_free (p->sector, upage);
      p->page_type = IN_MEMORY;
      p->sector = SECTOR_ERROR;
    }

    return true;
  }

  printf ("No such upage exists in supplementary page table\n");
  return false;
}


