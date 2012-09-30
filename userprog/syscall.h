#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>

void syscall_init (void);

bool is_ptr_valid (void *);
void check_pointer (void *);

void exit_handler (int);
int open_handler (const char *);

// lock for accessing the filesys code
static struct lock filesys_lock;

#endif /* userprog/syscall.h */
