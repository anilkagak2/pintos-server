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

bool allocator_insert_pte (void *kpage, void *pte);

unsigned allocator_hash (const struct hash_elem *e, void *);
bool allocator_less (const struct hash_elem *, const struct hash_elem *,void *);

/* Supplementary Page Table Structure & it's access methods.
   Every thread has it's own supplementary page table (organized
   as a Hash Table) */

// Types of pages
enum page_type_t
{
  IN_MEMORY,
  IN_FILE,
  ALL_ZERO,
  IN_SWAP
};

// Supplementary Page Table's Entry Structure
struct page
{
//  uint32_t *pte;		/* Page Table Entry. */
  enum page_type_t page_type;	/* Type of page. */

// you can get the kpage from the pte via pagedir_get_page ()
  void *kpage;			/* Frame's kernel address. */

  // also used for hashing
  void *upage;			/* Page. */

// can deal without is_present as there is one bit in the PTE for this
// PTE_P can be helpful in this regard (??)
  bool is_present;		/* kpage is valid (page is in memory). */
  bool writeable;		/* Is the page writable? */

  char file_name[20];		/* Name of the file(Need to remove size constraint) */
  // can offset be negative ?? but off_t defined int32_t in filesys
  int32_t file_ofs;		/* Offset within the file. */
  uint32_t read_bytes;		/* Bytes to read from page (< PGSIZE). */
  struct hash_elem elem;	/* Element to be inserted in hash table. */
};

void supplementary_init (struct hash *);
bool supplementary_less (const struct hash_elem *, const struct hash_elem *,void *);
unsigned supplementary_hash (const struct hash_elem *, void *);

void supplementary_exit ();
void supplementary_destroy_elem (struct hash_elem *e,void *aux);
//bool supplementary_insert (uint32_t *pte, void *kpage, const char *file_name,
bool supplementary_insert (void *upage, const char *file_name,
				int32_t ofs,uint32_t read_bytes, bool writeable,
				enum page_type_t type);
struct page *supplementary_lookup (void *upage);
bool supplementary_insert_kpage (void *upage, void *kpage);

#endif /* threads/palloc.h */
