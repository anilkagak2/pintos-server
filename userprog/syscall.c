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
struct file * search_fd_list (int fd);
struct file_descriptor *give_fdescriptor (int fd);

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

	check_pointer (file_name);

	// it's a critical section : accessing filesystem code
//	lock_acquire (&filesys_lock);
	f->eax = filesys_create (file_name, initial_size);
//	lock_release (&filesys_lock);

	break;
	}

    case SYS_REMOVE: {
	/* Delete a file. */
//	printf ("Remove is called\n");
	check_pointer (user_esp);
	const char *file_name = *user_esp;
	user_esp++;
	
	check_pointer (file_name);

	// it's a critical section : accessing filesystem code
//	lock_acquire (&filesys_lock);
	f->eax = filesys_remove (file_name);
//	lock_release (&filesys_lock);

	break;
	}

	// haven't inserted the file descriptors 0 & 1 into the fd_list
	// need to do it somewhere in initialization
    case SYS_OPEN: {
	/* Open a file. */
//	printf ("Open is called\n");
	check_pointer (user_esp);
	const char *file_name = *user_esp;
	user_esp++;
	
	check_pointer (file_name);

	// it's a critical section : accessing filesystem code
//	lock_acquire (&filesys_lock);
	struct file *fp = filesys_open (file_name);
//	lock_release (&filesys_lock);

	// need to set the f->eax value
	// could not open the file
	if (!fp)
		f->eax = -1;

	// need to give it a thought
	else {
		// will this be different on different invocations of this 
		// open () call
//		static struct file_descriptor fd;
//		fd.fp = fp;
//		list_insert (&thread_current ()->fd_list, &(fd.elem));
//		f->eax = (int)(&fd);
//		f->eax = (int)(fp);
		struct file_descriptor *fdptr = (struct file_descriptor *)malloc(sizeof(struct file_descriptor));

		if(!fdptr) {
			printf ("Cannot allocate memory to file descriptor pointer\n");
			file_close (fp);
			f->eax = -1;
		}	
		
		else {
			fdptr->fp = fp;
			fdptr->fd = t->fd_to_allot++;
//			printf ("Thread %s has fd_to_allot is %d\n",t->name, t->fd_to_allot);
			// it's a fault to use list_insert here (Why??)
			//list_insert (&t->fd_list, &fdptr->elem);
			list_push_back (&t->fd_list, &fdptr->elem);
			f->eax = fdptr->fd;
		}
	}

	break;
	}

    case SYS_FILESIZE: {
	/* Obtain a file's size. */
//	printf ("Filesize is called\n");

	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	struct file *fp = search_fd_list (fd);
	if (!fp) {
		printf ("No such file found in fd_list\n");
		f->eax = -1;
	}

	else {
		f->eax = file_length (fp);
	}

	break;
	}

    case SYS_READ: {
	/* Read from a file. */
//	printf ("Read is called\n");

	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	// reading from STDIN (usually keyboard)
	if (fd == STDIN_FILENO) {
		f->eax = input_getc ();
	}

	else {
		check_pointer (user_esp);
		char *buffer = *user_esp;
		user_esp++;

		// check the validity of the user pointer
		check_pointer (buffer);

		check_pointer (user_esp);
		unsigned size = *user_esp;
		user_esp++;

		struct file *fp = search_fd_list (fd);
		if (!fp) {
	//		printf ("No such file found in fd_list\n");
	//		f->eax = -1;
			exit_handler (-1);
		}

		else {
			f->eax = file_read (fp, buffer,size);
		}
	}
	break;
	}

    case SYS_WRITE: {
	/* Write to a file. */
	//printf ("Write is called\n");
	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	check_pointer (user_esp);
	char *buffer = *user_esp;
	user_esp++;

	// check the validity of the user pointer
	check_pointer (buffer);

	check_pointer (user_esp);
	uint32_t size = *user_esp;
	user_esp++;

	if (fd == STDOUT_FILENO)
		putbuf (buffer, size);

	// currently writing to STDIN kills the thread
	else {
		struct file *fp = search_fd_list (fd);
		if (!fp) {
			exit_handler (-1);
		}

		else {
			f->eax = file_write (fp, buffer,size);
		}
	}

	// is it required to change the t->ret_value after a system call??
	// think over it, it may create problems in future??
	// Answer seems No: ret_value is for the parent to see what was the status on exit
//	t->ret_value = 0;
//	f->eax = 0;
	break;
	}

    case SYS_SEEK: {
	/* Change position in a file. */
	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	check_pointer (user_esp);
	uint32_t pos = *user_esp;
	user_esp++;

	struct file *fp = search_fd_list (fd);
	if (!fp) {
		exit_handler (-1);
	}

	else {
		file_seek (fp, pos);
	}

	break;
	}

    case  SYS_TELL: {
	/* Report current position in a file. */
	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	struct file *fp = search_fd_list (fd);
	if (!fp) {
		exit_handler (-1);
	}

	else {
		f->eax = file_tell (fp);
	}

	break;
	}

    case SYS_CLOSE:
	/* Close a file. */
	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	struct file_descriptor *fdptr = give_fdescriptor (fd);
	if (!fdptr) {
		exit_handler (-1);
	}

	else {
		f->eax = list_remove (&fdptr->elem);
		free (fdptr);
	}

	break;
  }

}

// hepler function for exit system call
// also useful in check_pointer () for exiting the call
void exit_handler (int ret_value) {
	/* Terminate this process. */
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

	// free the memory used by the file descriptor structures
	struct list *fd_list = &t->fd_list;
	struct list_elem *e;

	while (!list_empty (fd_list)) {
		e = list_pop_front (fd_list);
		struct file_descriptor *fdptr = list_entry (e, struct file_descriptor, elem);
		file_close (fdptr->fp);
		free (fdptr);
	}

	thread_exit ();
}

// searches the thread's fd_list for the presence of 
// fd, if found returns the struct file* for that
// else return NULL
struct file *
search_fd_list (int fd)
{
	struct list *fd_list = &thread_current ()->fd_list;
	struct list_elem *e;

	for (e = list_begin (fd_list); e !=  list_end (fd_list); e = list_next (e)){
		struct file_descriptor *fdptr = list_entry (e, struct file_descriptor, elem);

		if (fdptr->fd == fd)
			return fdptr->fp;
	}
	return NULL;
}

struct file_descriptor *
give_fdescriptor (int fd)
{
	struct list *fd_list = &thread_current ()->fd_list;
	struct list_elem *e;

	for (e = list_begin (fd_list); e !=  list_end (fd_list); e = list_next (e)){
		struct file_descriptor *fdptr = list_entry (e, struct file_descriptor, elem);

		if (fdptr->fd == fd)
			return fdptr;
	}
	return NULL;
}
