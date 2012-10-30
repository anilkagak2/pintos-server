#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "page.h"
#include <stdbool.h>
#include <hash.h>

/* Frame Table Structure & it's access methods. */

// Frame Table Entry structure
struct frame
{
  void *kpage;		/* Kernel Virtual address of physical frame. */
  void *upage;		/* Address of the page residing at this frame. */
  struct thread *t;	/* Struct thread * of the thread. */
  bool free;			/* Is frame free? */
  struct hash_elem elem;	/* Hash element to be embedded in hash table. */
};

void allocator_init (void);
//void *allocator_get_page (void*);
//void *allocator_get_page (void*, enum page_type_t);
void *allocator_get_page (void*, enum page_type_t, bool writable);
void *allocator_get_free_page (void*);
void allocator_free_page (void *);
void allocator_exit (void);
void allocator_destroy_elem (struct hash_elem *,void *);

void release_frame_table_lock (void);
bool  is_frame_table_lock_acquired (void);
void allocator_dummy (void);

unsigned allocator_hash (const struct hash_elem *e, void *);
bool allocator_less (const struct hash_elem *, const struct hash_elem *,void *);

#endif /* frame.h */
