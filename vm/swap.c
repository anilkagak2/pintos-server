#include "swap.h"
#include "devices/disk.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include <bitmap.h>
#include <debug.h>

static struct lock swap_table_lock;
static struct bitmap *swap_table;

/* Swap Table access methods. */
void swap_init () 
{
  struct disk *swap = disk_get (1,1);	// swap 1:1 channel:device no
  if (!swap)
    PANIC ("No swap disk found\n");

  disk_sector_t size = disk_size (swap);

  swap_table = bitmap_create (size);
  if (!swap_table)
    PANIC ("Cannot create bitmap for the swap table\n");

  lock_init (&swap_table_lock);
}

// returns the start sector of the swap slot where upage is written
// all the vacant swap slots are set to FALSE
size_t
swap_write (void *kpage)
{
//  ASSERT (kpage);
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
    //disk_write (swap, idx + offset, upage + (offset * DISK_SECTOR_SIZE));
    disk_write (swap, idx + offset, kpage + (offset * DISK_SECTOR_SIZE));
  }

  lock_release (&swap_table_lock);
  return idx;
}

// all the used swap slots are set to TRUE
void
swap_read_and_free (disk_sector_t idx, void *upage)
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
  //  printf ("Swap read sector %d at address %p\n",idx+offset, upage +(offset * DISK_SECTOR_SIZE));
    disk_read (swap, idx + offset, upage + (offset * DISK_SECTOR_SIZE));
  }

  // vacant the swap slot
  bitmap_set_multiple (swap_table, idx, SLOT_SIZE, false);

  lock_release (&swap_table_lock);
//  printf ("Swap read called at %d for %p\n",idx,upage);
}

void swap_exit ()
{
  lock_acquire (&swap_table_lock);
  bitmap_destroy (swap_table);
  lock_release (&swap_table_lock);
}


