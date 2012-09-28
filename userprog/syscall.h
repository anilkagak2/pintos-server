#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>

void syscall_init (void);

bool is_ptr_valid (void *);
void check_pointer (void *);

#endif /* userprog/syscall.h */
