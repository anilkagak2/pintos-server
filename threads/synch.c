/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on.

   If semaphore's value is 0, then do priority donation.
   Insert the current thread to sema's waiters list, keeping order. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      struct thread *t = thread_current ();
        list_insert_ordered (&sema->waiters, &t->elem, less_priority, NULL);
      thread_block ();
    }

  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;

  if (!list_empty (&sema->waiters)) 
  {
//    thread_unblock (list_entry (list_pop_front (&sema->waiters),
  //                              struct thread, elem));
    struct list_elem *to_remove = list_max (&sema->waiters, more_priority, NULL);
    list_remove (to_remove);
    thread_unblock (list_entry (to_remove,
                                struct thread, elem));
  }
 // else
  //  thread_set_priority (thread_current ()->base_priority);
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
//  list_push_back (&all_locks, &lock->allelem);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep.

   Before doing sema_down(), add lock to the thread's list of locks & after
   coming out of it, remove this lock. This function call may be preemted so
   don't perform list_push_back() & list_pop_back() instead use list_remove().  */
// what about the lock_try_acquire ?? think about it
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);

  struct thread *t = thread_current ();
  if (lock->holder == t) {
  printf ("Lock holder: %d & current thread: %d & lock %p & supplementlock %p\n",lock->holder->tid, t->tid,lock,&t->supplement_lock);
  }

  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  enum intr_level old_level;
  
  old_level = intr_disable ();
  struct semaphore *sema = &lock->semaphore;

  // push the lock to the list of locks of thread in Critical Section
//  list_push_back (&t->locks_waiting_on, &lock->elem);	
//  printf ("Lock Holder is %s\n",(lock->holder)->name);
  t->lock_waiting_on = lock;
  while (sema->value == 0) 
    {
      list_insert_ordered (&sema->waiters, &t->elem, less_priority, NULL);

      ASSERT (!list_empty (&sema->waiters));
      ASSERT (lock->holder != NULL);

      donate_priority (lock->holder, t->priority);
      thread_block ();
    }

  // remove the lock from the list of locks thread is waiting on
//  list_remove (&lock->elem);

  // add lock to the list of locks thread is holding
  list_push_front (&t->locks_holding, &lock->elem);
  t->lock_waiting_on = NULL;
  sema->value--;

  //ASSERT (&lock->elem != NULL);
  lock->holder = t;
  intr_set_level (old_level);
//  printf ("Lock aquired.. by %s\n", t->name);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

/* Returns True if the priority of the lock l1's waiter is less than that of
   lock l2. 
   a & b are embedded in struct lock. */
bool
more_priority_lock (const struct list_elem *a,
		    const struct list_elem *b,
		    void *aux)
{
  struct lock *l1 = list_entry (a, struct lock, elem);
  struct lock *l2 = list_entry (b, struct lock, elem);

  struct thread *t1 = list_entry (list_begin (&((l1->semaphore).waiters)), struct thread, elem);
  struct thread *t2 = list_entry (list_begin (&((l2->semaphore).waiters)), struct thread, elem);

//  return t1->priority > t2->priority;
  return t1->priority < t2->priority;
}

void
lock_release (struct lock *lock) 
{
  //printf ("Lock holder: %s & current thread: %s\n",lock->holder->name, thread_current ()->name);
/*  if (lock->holder != thread_current ()) {
  printf ("Lock holder: %d & current thread: %d & lock %p\n",lock->holder->tid, thread_current ()->tid,lock);
  }*/

  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  enum intr_level old_level;
  struct semaphore *sema = &lock->semaphore;
  struct thread *t = thread_current ();

  ASSERT (sema != NULL);

  old_level = intr_disable ();

  lock->holder = NULL;

  /* thread_unblock () call may call yield () & in that case if you increase the semaphore's value later
   waiter will not be able to occupy the semaphore. */
  sema->value++;

  // remove this lock from thread's lock_holding list
  list_remove (&lock->elem);

  // if current thread got the donation from some waiter
  // need to return to the priority which was assigned before acquiring this lock
  // set the priority to be the maximum of {maximum priority among waiters in locks_list & base_priority}
  if (t->priority != t->base_priority)
  {
    // pick up the next lock in lock_holding with maximum waiter's priority
//    struct lock *locks_holding = &t->locks_holding;
    if (list_empty (&t->locks_holding))
	t->priority = t->base_priority;

    // thread still holds some locks
    else
    {
      struct thread *next_priority_assigner;
      struct lock *highest_priority_lock = list_entry (list_max (&t->locks_holding, more_priority_lock, NULL), struct lock, elem);
      next_priority_assigner = list_entry (list_begin (&(highest_priority_lock->semaphore.waiters)), struct thread, elem);

      // if the thread's base priority is less than next_priority_assigner's priority, assume that to be a donation
      if (t->base_priority < next_priority_assigner->priority)
		t->priority = next_priority_assigner->priority;
      else
		t->priority = t->base_priority;
    }
  }

  // thread_unblock () will call thread_yield () in case the this thread's priority is less than fron of readyQ.
  if (!list_empty (&sema->waiters)) 
  {
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  }

  // will yield the CPU if next thread in readyQ is having more priority
  else
  {
//    thread_set_priority (t->priority);
//   struct thread *next = list_entry (list_begin (&ready_list), struct thread, elem);
//   if (t->priority < next->priority)
#ifndef USERPROG
	thread_yield ();
#endif

   }

  // sema->value++;
  intr_set_level (old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    int priority;			/* Priority of this thread. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Returns True if priority of e1 > e2,
   otherwise False.
   Also check for the magic number of the threads. */
bool cond_less_priority (const struct list_elem *a,
			    const struct list_elem *b,
			    void *aux)
{
	struct semaphore_elem *e1 =  list_entry (a, struct semaphore_elem, elem);
	struct semaphore_elem *e2 =  list_entry (b, struct semaphore_elem, elem);

	return e1->priority > e2->priority;
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  waiter.priority = thread_current ()->priority;
//  list_push_back (&cond->waiters, &waiter.elem);
    list_insert_ordered (&cond->waiters, &waiter.elem, cond_less_priority, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
