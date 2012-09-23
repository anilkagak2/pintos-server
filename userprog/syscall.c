#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// checks the validity of user provided addresses
bool is_ptr_valid (void *ptr)
{
  return (ptr != NULL && is_user_vaddr (ptr) && 
		pagedir_get_page (thread_current ()->pagedir, ptr));
}

static void
//syscall_handler (struct intr_frame *f UNUSED) 
syscall_handler (struct intr_frame *f) 
{
//  printf ("system call!\n");

  uint32_t *user_esp = f->esp;
  size_t size_int = sizeof (int *);

  // may be required to handle the page fault
  ASSERT (is_ptr_valid (user_esp));

  // get the struct thread for calling process
  struct thread *t = thread_current ();

  uint32_t syscall_no = *user_esp;

  // pop the value off the stack
  user_esp++;
  ASSERT (is_user_vaddr (user_esp));

  // updating the stack pointer in the kernel stack
  //*(int **)(f->esp) = user_esp;
//  f->esp = user_esp;

  switch (syscall_no) {
    case SYS_HALT:
	/* Halt the operating system. */
	printf ("Halt is called\n");
	power_off ();
	break;

    case SYS_EXIT: {
	//printf ("Exit is called\n");
	/* Terminate this process. */

	uint32_t ret_value = *user_esp;
  	user_esp++;

	ASSERT (is_ptr_valid (user_esp));

	// not required as the syscall infrastructure does this for you
	// updating the stack pointer in the kernel stack
//	f->esp = user_esp;

	t->ret_value = ret_value;
	f->eax = ret_value;
	// calling sema_up may wake up the main process
	// waiting in process_wait, you need to synchronize it
//	sema_up (&t->sema);

	// destroy the pagedir of thread t
//	uint32_t *pd;

	/* Destroy the current process's page directory and switch back
	     to the kernel-only page directory. */
//	pd = t->pagedir;
//	if (pd != NULL) 
//	{
	      /* Correct ordering here is crucial.  We must set
        	 cur->pagedir to NULL before switching page directories,
	         so that a timer interrupt can't switch back to the
	         process page directory.  We must activate the base page
	         directory before destroying the process's page
        	 directory, or our active page directory will be one
	         that's been freed (and cleared). */
/*	      t->pagedir = NULL;
	      pagedir_activate (NULL);
	      pagedir_destroy (pd);
	}
*/	// need to implement the thread_exit () 

	thread_exit ();
	break;
	}

    case SYS_EXEC:
	/* Start another process. */
	printf ("Exec is called\n");
	break;

    case SYS_WAIT:
	/* Wait for a child process to die. */
	printf ("Wait is called\n");
	break;

    case SYS_CREATE:
	/* Create a file. */
	printf ("Create is called\n");
	break;

    case SYS_REMOVE:
	/* Delete a file. */
	printf ("Remove is called\n");
	break;

    case SYS_OPEN:
	/* Open a file. */
	printf ("Open is called\n");
	break;

    case SYS_FILESIZE:
	/* Obtain a file's size. */
	printf ("Filesize is called\n");
	break;

    case SYS_READ:
	/* Read from a file. */
	printf ("Read is called\n");
	break;

    case SYS_WRITE: {
	/* Write to a file. */
	//printf ("Write is called\n");

	uint32_t file_desc = *user_esp;
	user_esp++;
	ASSERT (is_ptr_valid (user_esp));

	char *buf = *user_esp;
	user_esp++;
	ASSERT (is_ptr_valid (user_esp));

	uint32_t len = *user_esp;
	user_esp++;
	ASSERT (is_ptr_valid (user_esp));

	// updating the stack pointer in the kernel stack
//	f->esp = user_esp;

	if (file_desc == STDOUT_FILENO)
		putbuf (buf, len);

	t->ret_value = 0;
	f->eax = 0;
	break;
	}

    case SYS_SEEK:
	/* Change position in a file. */
	printf ("Seek is called\n");
	break;

    case  SYS_TELL:
	/* Report current position in a file. */
	printf ("Tell is called\n");
	break;

    case SYS_CLOSE:
	/* Close a file. */
	printf ("Close is called\n");
	break;

  }

//  thread_exit ();
}
