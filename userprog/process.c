#include "userprog/process.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "vm/frame.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
bool load (const char *cmdline, void (**eip) (void), void **esp);
//static bool load (const char *cmdline, void (**eip) (void), void **esp);
static char ** cmd_token (char *cmdline, void *aux);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  struct semaphore sema;
  sema_init (&sema,0);

  char **kargv = cmd_token (fn_copy,(void *)&sema);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (kargv[0], PRI_DEFAULT, start_process, kargv);
  if (tid == TID_ERROR)
    palloc_free_page (kargv[0]); 

  // wait for the child to load it's executable
  else
  {
    sema_down (&sema);

    // check the load ()'s return value
    if (!kargv[0])
	return TID_ERROR;
  }

  return tid;
}

#define MAX_ARGS 30

/* Breaks the cmdline into tokens & returns the argv-like array. */
static char **
cmd_token (char *cmdline, void *aux)
{
  ASSERT (aux != NULL);

  //static char *argv[MAX_ARGS];
  static char *argv[MAX_ARGS];
  char *saveptr;
  int i=0;

  argv[i] = strtok_r (cmdline, " " ,&saveptr);

  if (argv[0] == NULL)
  {
    PANIC ("commandline given is not valid");
  }

  else
  {
    while (argv[i] != NULL) {
//	 printf ("argv[%d] %s\n",i,argv[i]);
	 argv[++i] = strtok_r (NULL," ",&saveptr);
    }

    argv[i] = (char *)aux;
    argv[++i] = NULL;
  }

  return argv;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *kargv)
{
  ASSERT (is_kernel_vaddr (kargv));
  char **argv = (char **)kargv;
  uintptr_t ptr = vtop (kargv);
  struct intr_frame if_;
  bool success;

  // initialize the supplementary page table for the thread
  supplementary_init (&thread_current ()->supplement_pt);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  static int argc = 0;
  while (argv[argc] != NULL)
	argc++;

  // last argument is a semaphore address & not a char*
  argc--;
  struct semaphore *sema = (struct semaphore *)argv[argc];
  argv[argc] = NULL;

/*  // open process's executable & call file_deny_write () on it
  int fd_exe = open_handler (argv[0]);

  // Accessing the filesys code, it's Critical Section
//  lock_acquire (&filesys_lock);

  if (fd_exe != -1) {
    struct file *fp = search_fd_list (fd_exe);
    file_deny_write (fp); 
  }
//  lock_release (&filesys_lock);
*/
  success = load (argv[0], &if_.eip, &if_.esp);

  // open process's executable & call file_deny_write () on it
  int fd_exe = open_handler (argv[0]);

  if (fd_exe != -1) {
    struct file *fp = search_fd_list (fd_exe);
    file_deny_write (fp);
  }

  if (success) {
        int i = argc - 1;
        int count = 0;
        size_t ptr_size = sizeof (char *);
	uint32_t offset[MAX_ARGS];

	// argv[argc-1] --> argv[0]
	// may be need to look till strlen(argv[i]) +1
	for (;i >= 0; i--) {
                if_.esp -= strlen (argv[i]) + 1;
                memcpy (if_.esp,argv[i], strlen (argv[i]) + 1);
                count += strlen (argv[i]) + 1;
		offset[i] = if_.esp;
        }

        // word align the stack pointer
        if ( ((uint32_t)if_.esp % 4) != 0) {
		int decrease = ((uint32_t)if_.esp % 4);
	// i have to keep track of the offset till argv[0] & not the alignment
                count += decrease;
                if_.esp -= decrease;
        }

	// argv[argc] = NULL
        if_.esp -= ptr_size;
        *(char **)(if_.esp) = NULL;

	// place the pointers to the argv's on the stack
	i = argc - 1;
	for (;i >= 0; i--) {
                if_.esp -= ptr_size;
                *(char **)(if_.esp) = offset[i];
        }

        if_.esp -= ptr_size;
        *(char **)if_.esp = if_.esp + ptr_size;             // argv

        if_.esp -= ptr_size;
        *(int *)if_.esp = argc;               // argc

        if_.esp -= ptr_size;
        *(int **)if_.esp = 0;             // fake return address 
  }

  /* If load failed, quit. */
  palloc_free_page (argv[0]);

  // since argv is a static variable, i put the status of success  or failure
  // load () in this thread, at that place
  argv[0] = (char *)success;
  sema_up (sema);	// increment the sema's value on which process_execute was waiting

  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  // search for the thread with tid_t child_tid, if not found return -1
  // or keep on waiting for it, till it is in the all_list 
  struct list_elem *e;
  bool childFound = false;

  struct list *children = &thread_current ()->children;
  struct child_info *t;

  // traverse the children list for the thread child_tid
  for (e = list_begin (children); e != list_end (children); e = list_next (e)) {
   // currently children list contains dynamically allocated struct
    t = list_entry (e, struct child_info, elem);

    if (t->tid == child_tid) {
      childFound = true;
      break;
    }
  }

  if (childFound) {
    // child is still alive
    if ( t->child != NULL ) {
	sema_down (&t->child->parent_sema);
	list_remove (e);
	int32_t ret_val = t->return_value;

	// should work without this if the ichild concept works fine
	// signal the thread to call thread_exit ()
//	sema_up (&thread_current ()->child_sema);

	// free the allocated struct
	free (t);

	return ret_val;
    }

    // child dead :(
    else {
	int ret_val = t->return_value;

	list_remove (e);
	free (t);

	return ret_val;
    }
  }

  // child_tid is not a child of current thread
  else return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the supplemental page table. */
  supplementary_exit ();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
/*static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);*/ 
//static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable, const char *file_name); 

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }

   //           if (!load_segment (file, file_page, (void *) mem_page,
     //                            read_bytes, zero_bytes, writable))
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable, file_name))
                goto done;
            }

          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

//static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
//static bool
bool
//load_segment (struct file *file, off_t ofs, uint8_t *upage,
  //            uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable,
		const char *file_name) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);

  // total offset 
  off_t offset = ofs;

  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

// do not ask for a page here, instead just let a page fault occur
// but add entry corresponding to the page in supplemental PT

      // add the entry for this virtual page in supplemental page
      // ALL_ZERO page
      if (page_read_bytes == 0) {
//	printf ("Thread %d ALL_ZERO page at load segment file %s offset %d read_bytes %d zero bytes %d\n",thread_current ()->tid, file_name, offset,page_read_bytes,page_zero_bytes);
        supplementary_insert_without_lock (upage, "", 0, 0, writable, ALL_ZERO);
      }

       // need to read from FILE
      else { 
//	printf ("Thread %d IN_FILE page at load segment file %s offset %d read_bytes %d writable %d\n",thread_current ()->tid, file_name, offset,page_read_bytes,writable);
        supplementary_insert_without_lock (upage, file_name, offset, page_read_bytes, writable, IN_FILE);
      }

      // increment the page offset
      offset += page_read_bytes;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  struct thread *t = thread_current ();
  uint8_t *upage = (uint8_t *)PHYS_BASE - PGSIZE;
  if (allocator_get_page (upage, ALL_ZERO, true)) {
    *esp = PHYS_BASE;

    // cannot go beyond this memory location
    t->user_stack_limit = upage;
    // maximum number of stack pages left to be allocated to this process
    t->num_stack_pages_left = 31;  // 1 is allocated here

    return true;
  }
  else return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
