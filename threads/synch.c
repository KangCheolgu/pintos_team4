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
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/*
   value의 값이 양수가 될때 까지 기다렸다가 sema의 value를 0으로 만들어 들어올수 없도록 함

   세마포어(SEMA)의 값을 양수가 될 때까지 대기하고, 그 후에 원자적으로 값을 감소시킵니다.
   이 함수는 슬립(sleep)할 수 있으므로, 이 함수는 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 슬립하는 경우에는 다음 스케줄된 스레드가 아마도 다시 인터럽트를 활성화할 것입니다.

   Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function my sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. 
*/
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;
	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {   
      list_insert_ordered(&sema->waiters, &thread_current ()->elem,
         cmp_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/*
   세마포어(SEMA)에 대한 "P(Proberen)" 또는 "Down" 연산을 수행하지만, 세마포어의 값이 이미 0인 경우에는 연산을 수행하지 않고,
   바로 false를 반환합니다. 세마포어가 성공적으로 감소되면 true를 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. 

   Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
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

/* 
   세마포어(SEMA)에 대한 "V(Verhogen)" 또는 "Up" 연산을 수행합니다. 이 함수는 세마포어의 값을 1 증가시키고,
   대기 중인 스레드 중 하나를 깨워서 실행 가능한 상태로 만듭니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다.

   Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	old_level = intr_disable ();

	if (!list_empty (&sema->waiters)){
      list_sort(&sema->waiters, cmp_priority_in_synch, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters), 
         struct thread, elem));
   }
   
	sema->value++;
   thread_preemption();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 
   세마포어에 대한 자체 테스트 코드입니다. 이 코드는 두 개의 스레드 간에 제어를 "ping-pong"하는 방식으로 동작합니다.
   코드의 실행 상황을 확인하기 위해 printf() 함수 호출을 삽입하세요.

   Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
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
sema_test_helper (void *sema_) {
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
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

   struct thread *curr = thread_current();   
   // printf("\n %s \n", curr->name);
   // lock holder 가 이미 있는 경우 wait lock 에 현재 락을 저장한 뒤 
   if(!thread_mlfqs){
      if (lock->holder != NULL) {
         // 필요한 락을 저장
         curr->wait_lock = lock;
         // // 기부자들을 저장
         list_insert_ordered(&lock->holder->donated_threads, &curr->d_elem, 
            cmp_priority_in_donate, NULL);
         // 우선순위를 기부
         priority_nested_donate(curr, lock, 8);
      }
   }
   //////////////////////////////////
   sema_down (&lock->semaphore);
   //////////////////////////////////
   curr->wait_lock = NULL;
   lock->holder = curr;

}

void priority_nested_donate(struct thread *donation, struct lock *lock, int depth) {
   // 만약 기부 받은 우선순위가 자기보다 크면 자기의 우선순위와 바꾼다
   struct thread *curr = donation;

   for(int i = 0; i < depth; i++){
      
      if (curr->wait_lock == NULL) break;

      struct thread *holder = curr->wait_lock->holder;

      holder->priority = curr->priority;

      curr = holder;
   }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
   
   struct thread *curr = thread_current();
   struct list_elem *e;

   if(!thread_mlfqs){
   // 릴리즈 된 락을 기다리는 스레드들은 기부해준 스레드 리스트에서 없애준다.

      for(e = list_begin(&curr->donated_threads);
         e != list_end(&curr->donated_threads);
         e = list_next(e)){
         struct thread *tmp = list_entry(e, struct thread, d_elem );
         if(tmp->wait_lock == lock){
            list_remove(&tmp->d_elem);
         }
      }
      select_maximum_donation(curr);
   }
   // printf("\n %s %d \n", lock->holder->name, curr->priority );
   lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
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
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
   //list_push_back(&cond->waiters, &waiter.elem);
   list_insert_ordered (&cond->waiters, &waiter.elem, cmp_sema_priority, NULL);
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
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

   list_sort(&cond->waiters, cmp_sema_priority, NULL);

	if (!list_empty (&cond->waiters)){
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
   }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

bool cmp_priority_in_synch (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){

   ASSERT(a != NULL);
	ASSERT(b != NULL);
	struct thread *a_entry = list_entry (a, struct thread, elem);
	struct thread *b_entry = list_entry (b, struct thread, elem);

	if(a_entry->priority > b_entry->priority) {
		return true;
	} else {
		return false;
	}
}

bool cmp_sema_priority (const struct list_elem *a,const struct list_elem *b, void *aux UNUSED){

   ASSERT(a != NULL);
	ASSERT(b != NULL);
	struct semaphore_elem *a_entry = list_entry (a, struct semaphore_elem, elem);
	struct semaphore_elem *b_entry = list_entry (b, struct semaphore_elem, elem);

   struct thread *a_thread = list_entry(list_begin(&a_entry->semaphore.waiters),struct thread, elem);
   struct thread *b_thread = list_entry(list_begin(&b_entry->semaphore.waiters),struct thread, elem);

	if(a_thread->priority > b_thread->priority) {
		return true;
	} else {
		return false;
	}
}

bool cmp_priority_in_donate (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){

   ASSERT(a != NULL);
	ASSERT(b != NULL);
	struct thread *a_entry = list_entry (a, struct thread, d_elem);
	struct thread *b_entry = list_entry (b, struct thread, d_elem);

	if(a_entry->priority > b_entry->priority) {
		return true;
	} else {
		return false;
	}
}