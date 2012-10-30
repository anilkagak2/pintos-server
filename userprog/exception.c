#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "userprog/syscall.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

// was the fault caused because of stack access past the limit?
  uint8_t *limit = thread_current ()->user_stack_limit;
//  if (fault_addr <= limit && fault_addr >= limit - 32) {
// PUSH(4) & PUSHA (32)
  if (fault_addr == f->esp - 4 ||  fault_addr == f->esp - 32) {
//  if (fault_addr <= limit && fault_addr >= limit - PGSIZE) {
    if (!stack_check (f->esp)) {
      exit_handler (-1);
    }

    return;
  }

  // if page fault caused by user access to invalid page should check for
  // the validity of page may be NULL was referenced or a valid page was
  // referenced, will be implemented in Virtual Memory
  if (user) {

//    struct page *p = supplementary_lookup (fault_addr);
// convert the fault_addr to a page
//    printf ("fault address %p \n", fault_addr);
    struct page *p = supplementary_lookup (pg_round_down (fault_addr));

    // invalid access
    if (!p) {
      printf ("Invalid address is %p & esp is %p\n & thread name is %s",fault_addr,f->esp,thread_current ()->name);
      exit_handler (-1);
    }

    uint32_t *upage = p->upage;

    // otherwise the page is in thread's page table but not in memory
    // need to load it in memory
    // need to get a free frame for allocating to this page
    switch (p->page_type) {
	case IN_MEMORY:
		printf ("No done yet\n");
		break;

	case IN_FILE: {
		lock_acquire (&filesys_lock);
		struct file *f = filesys_open (p->file_name);

		file_seek (f, p->file_ofs);

		uint8_t *kpage = allocator_get_page ();

// cannot allocate memory
		if (kpage == NULL) {
			lock_release (&filesys_lock);
			exit_handler (-1);
		}

		int page_read_bytes = p->read_bytes;

		if (file_read (f, kpage, page_read_bytes) != (int) page_read_bytes)
	        {
			allocator_free_page (kpage);
			lock_release (&filesys_lock);
			file_close (f);
			exit_handler (-1);
		}

		memset (kpage + page_read_bytes, 0, PGSIZE - page_read_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, p->writeable)) 
		{
			allocator_free_page (kpage);
			lock_release (&filesys_lock);
			file_close (f);
			exit_handler (-1);
		}

		// adding the page entry to supplemental page table
		else {
			uint32_t *pte = pagedir_search_page (thread_current ()->pagedir, upage);
			ASSERT (pte);

			// update supplemental page table
			supplementary_insert_kpage (upage, kpage);

			// update frame table
			allocator_insert_pte (kpage, pte);
		}

		file_close (f);
		lock_release (&filesys_lock);
		break;
	}

	case IN_SWAP:
		break;

	case ALL_ZERO: {
		uint8_t *kpage = allocator_get_page ();

		if (kpage == NULL)
			exit_handler (-1);

		memset (kpage, 0, PGSIZE);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, p->writeable)) 
		{
			allocator_free_page (kpage);
			exit_handler (-1);
		}

		// adding the page entry to supplemental page table
		else {
			uint32_t *pte = pagedir_search_page (thread_current ()->pagedir, upage);
			ASSERT (pte);

			// update supplemental page table
			supplementary_insert_kpage (upage, kpage);

			// update frame table
			allocator_insert_pte (kpage, pte);
		}
		break;
	}

	default:
		printf ("Not defined page type in page fault\n");
    }
  }

// kernel page fault
//  else
//    kill (f);

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
/*  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f); */
}

