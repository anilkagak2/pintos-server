#ifndef VM_SWAP_H
#define VM_SWAP_H

/* Swap Table Structure & it's access methods. */

#include <stddef.h>
#include "devices/disk.h"
#include "threads/vaddr.h"

#define SECTOR_ERROR SIZE_MAX
#define SLOT_SIZE (PGSIZE / DISK_SECTOR_SIZE)

void swap_init (void);
void swap_exit (void);

size_t swap_write (void *upage);
void swap_read_and_free (disk_sector_t idx, void *upage);

#endif	/* swap.h */
