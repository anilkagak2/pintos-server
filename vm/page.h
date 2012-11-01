#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "swap.h"
#include <debug.h>
#include "threads/thread.h"

/* Supplementary Page Table Structure & it's access methods.
   Every thread has it's own supplementary page table (organized
   as a Hash Table) */

// Types of pages
enum page_type_t
{
  IN_MEMORY,
  IN_FILE,
  ALL_ZERO,
  ALL_ZERO_I,
  IN_SWAP
};

// Supplementary Page Table's Entry Structure
struct page
{
  enum page_type_t page_type;	/* Type of page. */
  void *kpage;			/* Frame's kernel address. */
  void *upage;			/* Page. also used for hashing. */
  disk_sector_t sector;		/* Start sector number of the swap disk, where
 				   page is available, if need be. */
  bool writable;		/* Is the page writable? */
  char file_name[20];		/* Name of the file(Need to remove size constraint) */
  // can offset be negative ?? but off_t defined int32_t in filesys
  int32_t file_ofs;		/* Offset within the file. */
  uint32_t read_bytes;		/* Bytes to read from page (< PGSIZE). */
  struct hash_elem elem;	/* Element to be inserted in hash table. */
};

void supplementary_init (struct hash *);
bool supplementary_less (const struct hash_elem *, const struct hash_elem *,void *);
unsigned supplementary_hash (const struct hash_elem *, void *);

void supplementary_exit (void);
void supplementary_destroy_elem (struct hash_elem *e,void *aux UNUSED);

void supplementary_insert (void *upage, const char *file_name,
				int32_t ofs,uint32_t read_bytes, bool writable,
				enum page_type_t type);
void
supplementary_insert_without_lock (void *upage, const char *file_name,
				int32_t ofs,uint32_t read_bytes, bool writable,
				enum page_type_t type);
void supplementary_insert_zero_page (void *upage, void *kpage);
struct page *supplementary_lookup (struct thread *t, void *upage);
bool supplementary_insert_kpage (void *upage, void *kpage, enum page_type_t);

#endif /* Page.h */
