/* Frame Table Structure & it's access methods. */

/* Maximum number of pages to put in user pool. */
extern size_t user_page_limit;

// Frame Table Entry structure
struct frame
{
  void *kpage;		/* Kernel Virtual address of physical frame. */
  //void *pte;		/* Page table entry of the page residing at this frame. */
  void *upage;		/* Address of the page residing at this frame. */
  struct thread *t;	/* Struct thread * of the thread. */
  bool free;			/* Is frame free? */
  struct hash_elem elem;	/* Hash element to be embedded in hash table. */
};

struct frame frame_table[];		/* Frame Table. */
struct lock frame_table_lock;	/* Lock for synchronizing access to frame table. */

void allocator_init (void);
void *allocator_get_page (void);
void *allocator_get_free_page (void);
void allocator_free_page (void *);
void allocator_exit (void);

void allocator_dummy (void);

//bool allocator_insert_pte (void *kpage, void *pte);
bool allocator_insert_upage (void *kpage, void *upage);
void re_init_frame (struct frame *f);
