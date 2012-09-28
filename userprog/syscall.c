#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <syscall.h>
//#include "tests/lib.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
//void check_pointer (void *ptr);

void exit_handler (int ret_value);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// checks the validity of user provided addresses
bool is_ptr_valid (void *ptr)
{
  if (ptr == NULL) return false;
  else if (!is_user_vaddr (ptr)) return false;
  else if (!pagedir_get_page (thread_current ()->pagedir, ptr)) return false;
  else return true;

//  return (ptr != NULL && is_user_vaddr (ptr) && 
//		pagedir_get_page (thread_current ()->pagedir, ptr));
}

void check_pointer (void *ptr)
{
  if (!is_ptr_valid (ptr)) {
//    printf ("%s: exit(%d)\n",thread_current ()->name, -1);
//    thread_exit ();
//      exit (-1);
     exit_handler (-1);
  }
}

static void
//syscall_handler (struct intr_frame *f UNUSED) 
syscall_handler (struct intr_frame *f) 
{
//  printf ("system call!\n");

  uint32_t *user_esp = f->esp;
  size_t size_int = sizeof (int *);

  // check for the validity of the Stack Pointer
  // before calling anything because the thread_current ()
  // rounds down the value of esp to guess the struct thread *
  check_pointer (user_esp);

  // get the struct thread for calling process
  struct thread *t = thread_current ();

  // may be required to handle the page fault
//  ASSERT (is_ptr_valid (user_esp));
/*  if (!is_ptr_valid (user_esp)) {
	printf ("%s: exit(%d)\n",t->name, -1);
	thread_exit ();
  }*/
//  check_pointer (user_esp);
  uint32_t syscall_no = *user_esp;

  // pop the value off the stack
  user_esp++;

  // is it really required ??
  //ASSERT (is_user_vaddr (user_esp));
/*  if (!is_ptr_valid (user_esp)) {
	printf ("%s: exit(%d)\n",t->name, -1);
	thread_exit ();
  }*/
 // check_pointer (user_esp);

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
	/* Terminate this process. */
	check_pointer (user_esp);
	uint32_t ret_value = *user_esp;
  	user_esp++;

	exit_handler (ret_value);
/*	printf ("%s: exit(%d)\n",t->name, ret_value);

	t->ret_value = ret_value;
	f->eax = ret_value;
	// calling sema_up may wake up the main process
	// waiting in process_wait, you need to synchronize it
	// up your parent_semaphore 
	sema_up (&t->parent_sema);

	// now wait till the parent process get's the exit value
	// wait on parent's child semaphore
	sema_down (&t->parent->child_sema);

	thread_exit (); */
	break;
	}

    case SYS_EXEC: {
	/* Start another process. */
//	ASSERT (is_ptr_valid (user_esp));
/* 	if (!is_ptr_valid (user_esp)) {
	  printf ("%s: exit(%d)\n",t->name, -1);
	  thread_exit ();
	}*/
	check_pointer (user_esp);
	const char *cmd_line = *user_esp;
	user_esp++;

	if (is_ptr_valid (cmd_line))
		f->eax = process_execute (cmd_line);
	else f->eax = -1;

	break;
	}

    case SYS_WAIT: {
	/* Wait for a child process to die. */
//	ASSERT (is_ptr_valid (user_esp));
/*	if (!is_ptr_valid (user_esp)) {
	  printf ("%s: exit(%d)\n",t->name, -1);
	  thread_exit ();
	}*/
	check_pointer (user_esp);
	tid_t child_tid = *user_esp;
	user_esp++;

	f->eax = process_wait (child_tid);

	break;
	}

    case SYS_CREATE: {
	/* Create a file. */
	//printf ("Create is called\n");
	check_pointer (user_esp);
	const char *file_name = *user_esp;
	user_esp++;
	
	check_pointer (user_esp);
	size_t initial_size = *user_esp;
	user_esp++;

	//if (is_ptr_valid (file_name))
	check_pointer (file_name);
//	if (is_ptr_valid (file_name))
		f->eax = filesys_create (file_name, initial_size);
//	else f->eax = -1;

	break;
	}

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
	check_pointer (user_esp);
	uint32_t file_desc = *user_esp;
	user_esp++;

//	ASSERT (is_ptr_valid (user_esp));
/*	if (!is_ptr_valid (user_esp)) {
	  printf ("%s: exit(%d)\n",t->name, -1);
	  thread_exit ();
	}*/
	check_pointer (user_esp);
	char *buf = *user_esp;
	user_esp++;

//	ASSERT (is_ptr_valid (user_esp));
/*	if (!is_ptr_valid (user_esp)) {
	  printf ("%s: exit(%d)\n",t->name, -1);
	  thread_exit ();
	}*/
	check_pointer (user_esp);
	uint32_t len = *user_esp;
	user_esp++;

//	ASSERT (is_ptr_valid (user_esp));

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

}

// hepler function for exit system call
// also useful in check_pointer () for exiting the call
void exit_handler (int ret_value) {
	/* Terminate this process. */
//	check_pointer (user_esp);
//	uint32_t ret_value = *user_esp;
//  	user_esp++;

	struct thread *t = thread_current ();
	printf ("%s: exit(%d)\n",t->name, ret_value);

	t->ret_value = ret_value;
	// calling sema_up may wake up the main process
	// waiting in process_wait, you need to synchronize it
	// up your parent_semaphore 
	sema_up (&t->parent_sema);

	// now wait till the parent process get's the exit value
	// wait on parent's child semaphore
	sema_down (&t->parent->child_sema);

	thread_exit ();
}
