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
#include "threads/malloc.h"


/* One semaphore in a list. */
struct semaphore_elem 
{
  struct list_elem elem;              /* List element. */
  struct semaphore semaphore;         /* This semaphore. */
  int priority;                       /* The priority of 
                                         first item of semaphore waiters*/
};

static void priority_stack_push (struct thread *, struct lock *);
static int get_original_priority_from_lock (struct list * , struct lock *);

static void cond_push_by_priority (struct list *, struct semaphore_elem *);


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
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
  {
    /* If current thread has to sleep, push the thread to
       the sema->waiters by priority. */
    thread_push_by_priority (&sema->waiters, thread_current ());
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
  struct thread *t = NULL;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) {
    t = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
    thread_unblock (t);
  }
  sema->value++;
  intr_set_level (old_level);

  /* After sema_up, if the current thread's priority is smaller than
     the thread's which was popped from waiters of the sema,
     yield the current thread. */
  if (t != NULL && thread_get_priority () < t->priority)
    thread_yield ();
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
}

/* Push the original priority of lock_holder to the priority stack.
   But if there is a original priority derived from same lock, skip. */
static void
priority_stack_push (struct thread *lock_holder, struct lock *lock)
{
  struct lock_elem *le = NULL;
  struct list_elem *pos = NULL;
  struct list_elem *end = list_end (&lock_holder->priority_stack);

  /* Check there is a priority derived from same lock.  */
  for (pos = list_begin (&lock_holder->priority_stack); pos != end;
      pos = pos->next) {

    le = list_entry (pos, struct lock_elem, elem);
    if (le->lock == lock)
      return;
  }

  /* Push the new lock element to list. */
  le = (struct lock_elem *) malloc (sizeof (struct lock_elem));
  le->priority = lock_holder->priority;
  le->lock = lock;
  list_push_front (&lock_holder->priority_stack, &le->elem);
  list_push_front (&lock_holder->release_first, &le->rl_elem);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  int priority;
  enum intr_level old_level;
  struct lock *l = NULL;
  struct thread *lock_holder = NULL;
  struct thread *cur_t = thread_current ();

  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  /* If there is a holder of the lock, donate the priority to
     the holder. */
  if (!thread_mlfqs && lock->holder != NULL) {
    old_level = intr_disable ();

    priority = thread_get_priority ();
    lock_holder = lock->holder;

    /* Push holder and lock to priority stack. */
    priority_stack_push (lock_holder, lock);

    /* Modify the priority of lock_holder to new priority
       donated from the thread which acquired the lock. */
    thread_set_priority_and_repos (lock_holder, priority);

    /* If the holder's status is BLOCKED, it means there can be
       some possibility that the holder also acquired a lock.
       So repeat actions above until finding holder unblocked. */
    while (lock_holder->status == THREAD_BLOCKED
        && lock_holder->lock_acquired != NULL) {
      l = lock_holder->lock_acquired;
      lock_holder = l->holder;

      priority_stack_push (lock_holder, l);
      thread_set_priority_and_repos (lock_holder, priority);
    }

    /* Set that current thread is acquiring the lock. */
    cur_t->lock_acquired = lock;

    intr_set_level (old_level);
  }

  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
  cur_t->lock_acquired = NULL;
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

/* Get the original priority from the lock elem which has lock which
   will be released. */
static int
get_original_priority_from_lock (struct list *list, struct lock *lock)
{
  int result = -1;
  struct list_elem *pos = NULL;
  struct lock_elem *entry = NULL;
  struct thread *cur_t = thread_current ();

  for (pos = list_begin (list); pos != list_end (list); pos = pos->next) {

    entry = list_entry (pos, struct lock_elem, elem);

    if (entry->lock == lock) {

      if (&entry->rl_elem != list_begin (&cur_t->release_first)) {
        list_entry (
            list_begin (&cur_t->release_first), 
            struct lock_elem, 
            rl_elem)->priority
          = entry->priority;
      } else
        result = entry->priority;

      list_remove (pos);
      list_remove (&entry->rl_elem);
      free (entry);
      
      return result;
    }
  }

  return result;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  int priority;
  struct thread *cur_t = thread_current ();

  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;
  sema_up (&lock->semaphore);

  if (!thread_mlfqs && (priority = get_original_priority_from_lock
        (&cur_t->priority_stack, lock)) != -1) {
    thread_set_current_priority (priority);
  }
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

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Add the elem to list by priority. */
void
cond_push_by_priority (struct list *list, struct semaphore_elem *elem)
{
  struct semaphore_elem *se = NULL;
  struct list_elem *pos = NULL;
  struct list_elem *end = list_end (list);

  for (pos = list_begin (list); pos != end; pos = pos->next) {

    se = list_entry (pos, struct semaphore_elem, elem);

    if (elem->priority >= se->priority)
      break;
  }

  list_insert (pos, &elem->elem);
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
  /* Set the priority, which is of the current thread, to waiter.  */
  waiter.priority = thread_get_priority ();
  /* Push the waiter to waiter list of conditional value
     by priority */
  cond_push_by_priority (&cond->waiters, &waiter);
  //list_push_back (&cond->waiters, &waiter.elem);
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
