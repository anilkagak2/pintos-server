#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <syscall.h>
//#include "tests/lib.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "threads/palloc.h"

static void syscall_handler (struct intr_frame *);
//void check_pointer (void *ptr);

void exit_handler (int ret_value);
int open_handler (const char *file);

struct file * search_fd_list (int fd);
struct file_descriptor *give_fdescriptor (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init (&filesys_lock);
}

// checks the validity of user provided addresses
bool is_ptr_valid (void *ptr)
{
  if (ptr == NULL) return false;
  else if (!is_user_vaddr (ptr)) return false;
  else if (!pagedir_get_page (thread_current ()->pagedir, ptr)) return false;
  else return true;

/*  else {
    // if page mapping is absent in page directory
    // you need to allocate a page for this & create a mapping for it
    if (!pagedir_get_page (thread_current ()->pagedir, ptr)) {
	uint8_t *kpage = allocator_get_page ();

	if (kpage == NULL)
		exit_handler (-1);

	memset (kpage, 0, PGSIZE);
	uint8_t *upage = pg_round_down (ptr); */

	/* Add the page to the process's address space. */
/*	if (!install_page (upage, kpage, true)) 
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
    }

    return true;
  } */
}

bool
allocate_zeroed_page (void *upage) {
	uint8_t *kpage = allocator_get_page ();

	if (kpage == NULL)
		return false;

	memset (kpage, 0, PGSIZE);

	/* Add the page to the process's address space. */
	if (!install_page (upage, kpage, true)) 
	{
		allocator_free_page (kpage);
		return false;
	}

	// adding the page entry to supplemental page table
	else {
		uint32_t *pte = pagedir_search_page (thread_current ()->pagedir, upage);
		ASSERT (pte);

		// update supplemental page table
		supplementary_insert (upage, "", 0, 0, true, ALL_ZERO);
		supplementary_insert_kpage (upage, kpage);

		// update frame table
		allocator_insert_pte (kpage, pte);
	}
	return true;
}

// checks the validity of user provided buffer in read syscall
bool is_buffer_valid (void *ptr)
{
  if (ptr == NULL) return false;
  else if (!is_user_vaddr (ptr)) return false;

  else {
    // if page mapping is absent in page directory
    // you need to allocate a page for this & create a mapping for it
    if (!pagedir_get_page (thread_current ()->pagedir, ptr)) {
/*	if (!allocate_zeroed_page (pg_round_down (ptr)))
		exit_handler (-1); */
	return false;
    } 

    return true;
  }
}

void check_pointer (void *ptr)
{
  if (!is_ptr_valid (ptr)) {
     exit_handler (-1);
  }
}

bool
stack_check (uint8_t *esp)
{
  if (!esp) return false;
  else if (!is_user_vaddr (esp)) return false;
  else {
    struct thread *cur = thread_current ();
    uint8_t *limit = cur->user_stack_limit;

//  printf ("user stack limit is %p & accessed esp is %p\n",limit,esp);
    // if esp lies between limit - 32 & limit, then allocate new stack page
    if (esp <= limit && esp >= limit - 32) {
      uint8_t *upage;

      if (esp == limit) upage = esp - PGSIZE;
      else upage = pg_round_down (esp);

      if (!allocate_zeroed_page (upage)) {
//	printf ("Cannot allocate stack page %p\n", upage);
        return false;
      }

      else {
//	printf (" allocated stack page %p\n", upage);
        cur->user_stack_limit = upage;
	return true;
      }
    }
  }
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *user_esp = f->esp;
  size_t size_int = sizeof (int *);

  // check for the validity of the Stack Pointer
  // before calling anything because the thread_current ()
  // rounds down the value of esp to guess the struct thread *
  check_pointer (user_esp);

  // get the struct thread for calling process
  struct thread *t = thread_current ();

  // check the user stack growth possibility
  if (!stack_check ( (uint8_t *) user_esp))
    exit_handler;

  uint32_t syscall_no = *user_esp;

  // pop the value off the stack
  user_esp++;

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
	break;
	}

    case SYS_EXEC: {
	/* Start another process. */
	check_pointer (user_esp);
	const char *cmd_line = *user_esp;
	user_esp++;

	// Don't acquire the filesys lock here, it'll be wastage of the resource
	if (is_ptr_valid (cmd_line)) {
		lock_acquire (&filesys_lock);
		f->eax = process_execute (cmd_line);
		lock_release (&filesys_lock);
	}
	else f->eax = -1;

	break;
	}

    case SYS_WAIT: {
	/* Wait for a child process to die. */
	check_pointer (user_esp);
	tid_t child_tid = *user_esp;
	user_esp++;

	f->eax = process_wait (child_tid);

	break;
	}

    case SYS_CREATE: {
	/* Create a file. */
	check_pointer (user_esp);
	const char *file_name = *user_esp;
	user_esp++;
	
	check_pointer (user_esp);
	size_t initial_size = *user_esp;
	user_esp++;

	check_pointer (file_name);

	// it's a critical section : accessing filesystem code
	lock_acquire (&filesys_lock);
	f->eax = filesys_create (file_name, initial_size);
	lock_release (&filesys_lock);

	break;
	}

    case SYS_REMOVE: {
	/* Delete a file. */
	check_pointer (user_esp);
	const char *file_name = *user_esp;
	user_esp++;
	
	check_pointer (file_name);

	// it's a critical section : accessing filesystem code
	lock_acquire (&filesys_lock);
	f->eax = filesys_remove (file_name);
	lock_release (&filesys_lock);

	break;
	}

	// haven't inserted the file descriptors 0 & 1 into the fd_list
	// need to do it somewhere in initialization
    case SYS_OPEN: {
	/* Open a file. */
	check_pointer (user_esp);
	const char *file_name = *user_esp;
	user_esp++;
	
	check_pointer (file_name);

	// it's a critical section : accessing filesystem code
	lock_acquire (&filesys_lock);
	f->eax = open_handler (file_name);
	lock_release (&filesys_lock);

	break;
	}

    case SYS_FILESIZE: {
	/* Obtain a file's size. */
	check_pointer (user_esp);
	int fd = *user_esp;
	user_esp++;

	struct file *fp = search_fd_list (fd);
	if (!fp) {
		printf ("No such file found in fd_list\n");
		f->eax = -1;
	}

	else {
		lock_acquire (&filesys_lock);
		f->eax = file_length (fp);
		lock_release (&filesys_lock);
	}

	break;
	}

    case SYS_READ: {
	/* Read from a file. */
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
	//	check_pointer (buffer);
		// if buffer pointer is not valid

		check_pointer (user_esp);
		unsigned size = *user_esp;
		user_esp++;

//	printf ("Reading to this address %p, last address %p page down %p , page up %p & the size is %d\n",buffer, buffer + size,pg_round_down (buffer), pg_round_up (buffer),size);
		//if (!is_buffer_valid (buffer)) exit_handler (-1);
		if (!is_buffer_valid (buffer + size)) exit_handler (-1);

		// currently allowing reading across two pages, one is mapped
		if (!is_buffer_valid (buffer)) {	
	int available_size = (buffer + size) - (char *)pg_round_down (buffer+size);
	if (available_size <=  PGSIZE) {
		if (!allocate_zeroed_page (pg_round_down (buffer))) exit_handler (-1); }

	else exit_handler (-1);
		}

		// buffer + size is valid then allocate memory for it
/*		if (!is_buffer_valid (buffer)) {
		  int left_bytes = size;
		  while ( left_bytes > 0) {
	//	pg_round_down (buffer + left_bytes) pg_round_down (buffer) 
	int available_size = (buffer + left_bytes) - (char *)pg_round_down (buffer + left_bytes);
	left_bytes -= available_size;
	if (!is_buffer_valid (buffer + left_bytes))
		allocate_zeroed_page (pg_round_down (buffer + left_bytes));
	
//		    if (left_bytes > PGSIZE)
//		      left_byes -= PGSIZE;

//		    allocate_zeroed_page (pg_round_down (buffer + left_bytes)); 
 		  }
		} */

		struct file *fp = search_fd_list (fd);
		if (!fp) {
			exit_handler (-1);
		}

		else {
			lock_acquire (&filesys_lock);

			f->eax = file_read (fp, buffer,size);
			/*while ()
			int bytes_read = file_read ();
			f->eax = file_read (fp, buffer,size); */

			lock_release (&filesys_lock);
		}
	}
	break;
	}

    case SYS_WRITE: {
	/* Write to a file. */
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
			lock_acquire (&filesys_lock);
			f->eax = file_write (fp, buffer,size);
			lock_release (&filesys_lock);
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
		lock_acquire (&filesys_lock);
		file_seek (fp, pos);
		lock_release (&filesys_lock);
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
		lock_acquire (&filesys_lock);
		f->eax = file_tell (fp);
		lock_release (&filesys_lock);
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
		lock_acquire (&filesys_lock);
		file_close (fdptr->fp);
		lock_release (&filesys_lock);

		f->eax = list_remove (&fdptr->elem);
		free (fdptr);
	}

	break;
  }

}

struct child_info *
get_parents_child_info ()
{
  struct thread *cur = thread_current ();
  struct thread *par = cur->parent;
  tid_t tid = cur->tid;
  ASSERT (par);

  // search for the thread with tid_t child_tid, if not found return -1
  // or keep on waiting for it, till it is in the all_list 
  struct list_elem *e;
  bool childFound = false;

  struct list *children = &par->children;
  struct child_info *t;

  // traverse the children list for the thread child_tid
  for (e = list_begin (children); e != list_end (children); e = list_next (e)) {
   // currently children list contains dynamically allocated struct
    t = list_entry (e, struct child_info, elem);

    if (t->tid == tid) {
      childFound = true;
      break;
    }
  }

  if (childFound) return t;
  else return NULL;
}

// hepler function for exit system call
// also useful in check_pointer () for exiting the call
void exit_handler (int ret_value) {
	/* Terminate this process. */
	struct thread *t = thread_current ();
	printf ("%s: exit(%d)\n",t->name, ret_value);

	// child info
	//struct child_info *ichild = get_parents_child_info (t->tid);
	struct child_info *ichild = get_parents_child_info ();

	// you should add this if (ichild) check may be parent dies before
	// child
	if (ichild) {
		ichild->return_value = ret_value;
		ichild->child = NULL;
	}

	// may be if ichild concept works, you'll not be in need of this
	// field in struct thread
	t->ret_value = ret_value;

	// free the file descriptors so that parent can write on it
	// free the memory used by the file descriptor structures
	struct list *fd_list = &t->fd_list;
	struct list_elem *e;

	lock_acquire (&filesys_lock);

	while (!list_empty (fd_list)) {
		e = list_pop_front (fd_list);
		struct file_descriptor *fdptr = list_entry (e, struct file_descriptor, elem);
		file_close (fdptr->fp);
		free (fdptr);
	}

	lock_release (&filesys_lock);

	// release all the lock's which the thread is holding
	struct list *lock_list = &t->locks_holding;
	while (!list_empty (lock_list)) {
		e = list_pop_front (lock_list);
		struct lock *_lock = list_entry (e, struct lock,elem);

		lock_release (_lock);
	}

	// calling sema_up may wake up the main process
	// waiting in process_wait, you need to synchronize it
	// up your parent_semaphore 
	sema_up (&t->parent_sema);

	// what if parent gets killed or parent exits before child ??
	// this t->parent will not be a valid pointer then
	// now wait till the parent process get's the exit value
	// wait on parent's child semaphore
//	sema_down (&t->parent->child_sema);

	thread_exit ();
}

// helper function for the open system call
int open_handler (const char *file_name)
{
  // it's a critical section : accessing filesystem code
//  lock_acquire (&filesys_lock);
  struct file *fp = filesys_open (file_name);
//  lock_release (&filesys_lock);

  struct thread *t = thread_current ();

  // could not open the file
  if (!fp) return -1;

  else
  {
    struct file_descriptor *fdptr = (struct file_descriptor *)malloc(sizeof(struct file_descriptor));

    if(!fdptr) 
    {
	printf ("Cannot allocate memory to file descriptor pointer\n");
	file_close (fp);
	return -1;
    }	

    else
    {
	fdptr->fp = fp;
	fdptr->fd = t->fd_to_allot++;

	// it's a fault to use list_insert here (Why??)
	//list_insert (&t->fd_list, &fdptr->elem);
	list_push_back (&t->fd_list, &fdptr->elem);
	return fdptr->fd;
    }
  }
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
