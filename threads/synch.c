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

// -----------------------------------------------------------------
// [추가] 락 waiters 리스트를 정렬하는 데 사용될 비교 함수 선언
extern bool thread_compare_priority (const struct list_elem *a,
                                     const struct list_elem *b,
                                     void *aux UNUSED);
// -----------------------------------------------------------------


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
            // -------------------------------------------------------------
            // [수정] list_push_back 대신 list_insert_ordered 사용 (우선순위 정렬)
            list_insert_ordered (&sema->waiters, &thread_current ()->elem, 
                                 thread_compare_priority, NULL);
            // -------------------------------------------------------------
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
    if (!list_empty (&sema->waiters)) {
        // -------------------------------------------------------------
        // [수정] list_pop_front: 우선순위 순으로 정렬되었으므로, 맨 앞이 최고 우선순위
        struct thread *t = list_entry (list_pop_front (&sema->waiters),
                                       struct thread, elem);
        thread_unblock (t);
        
        // Unblock된 스레드의 우선순위가 현재 스레드보다 높으면 선점 유도
        if (t->priority > thread_current()->priority) {
            thread_yield();
        }
        // -------------------------------------------------------------
    }
    sema->value++;
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

/* Initializes LOCK. ... */
void
lock_init (struct lock *lock)
{
    ASSERT (lock != NULL);

    lock->holder = NULL;
    // -------------------------------------------------------------
    // [수정] semaphore 대신 waiters 리스트 초기화
    list_init (&lock->waiters);
    // -------------------------------------------------------------
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary. ... */
void
lock_acquire (struct lock *lock)
{
    struct thread *cur = thread_current();
    enum intr_level old_level;

    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (!lock_held_by_current_thread (lock));

    old_level = intr_disable ();

    if (lock->holder != NULL) {
        // -------------------------------------------------------------
        // [추가] Priority Donation 로직
        
        // 1. 현재 스레드가 기다리는 락 설정
        cur->wait_on_lock = lock;
        
        // 2. 현재 락 보유자의 donations 리스트에 이 락을 추가하고 우선순위 기부 전파
        list_insert_ordered (&lock->holder->donations, &lock->donation_elem, 
                             thread_compare_priority, NULL);
        thread_donate_priority(cur);

        // 3. lock의 waiters 리스트에 현재 스레드 삽입 (우선순위 순 정렬)
        list_insert_ordered (&lock->waiters, &cur->elem, thread_compare_priority, NULL);

        // 4. 블록
        thread_block ();

        // 5. Block에서 깨어났으므로, 더 이상 락을 기다리지 않음
        cur->wait_on_lock = NULL;
        // -------------------------------------------------------------
    }
    
    // 락 획득 성공 (처음 획득하거나, block에서 깨어난 경우)
    lock->holder = cur;

    intr_set_level (old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure. ... */
bool
lock_try_acquire (struct lock *lock)
{
    enum intr_level old_level;
    bool success = false;

    ASSERT (lock != NULL);
    ASSERT (!lock_held_by_current_thread (lock));

    old_level = intr_disable ();
    if (lock->holder == NULL)
        {
            lock->holder = thread_current ();
            success = true;
        }
    intr_set_level (old_level);

    return success;
}

/* Releases LOCK, which must be owned by the current thread. ... */
void
lock_release (struct lock *lock)
{
    struct thread *cur = thread_current();
    enum intr_level old_level;

    ASSERT (lock != NULL);
    ASSERT (lock_held_by_current_thread (lock));

    old_level = intr_disable ();
    
    // -------------------------------------------------------------
    // [추가] Priority Donation 로직
    
    // 1. donations 리스트에서 현재 해제하는 락 제거 및 우선순위 재계산
    thread_remove_lock(lock); 
    
    // 2. Lock의 waiters 리스트에서 최고 우선순위 스레드를 unblock
    if (!list_empty (&lock->waiters)) {
        struct thread *t = list_entry(list_pop_front(&lock->waiters), struct thread, elem);
        thread_unblock(t);
        
        // Unblock된 스레드의 우선순위가 현재 스레드보다 높으면 선점 유도
        if (t->priority > cur->priority) {
            thread_yield();
        }
    }

    // 3. 락 보유자 초기화
    lock->holder = NULL;
    // -------------------------------------------------------------
    
    intr_set_level (old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise. ... */
bool
lock_held_by_current_thread (const struct lock *lock)
{
    ASSERT (lock != NULL);

    return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
{
    struct list_elem elem;      /* List element. */
    struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND. ... */
void
cond_init (struct condition *cond)
{
    ASSERT (cond != NULL);

    list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code. ... */
void
cond_wait (struct condition *cond, struct lock *lock)
{
    struct semaphore_elem waiter;

    ASSERT (cond != NULL);
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (lock_held_by_current_thread (lock));

    sema_init (&waiter.semaphore, 0);
    // -------------------------------------------------------------
    // [수정] list_push_back 대신 list_insert_ordered 사용 (우선순위 정렬)
    list_insert_ordered (&cond->waiters, &waiter.elem, thread_compare_priority, NULL);
    // -------------------------------------------------------------
    lock_release (lock);
    sema_down (&waiter.semaphore);
    lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait. ... */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
    ASSERT (cond != NULL);
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (lock_held_by_current_thread (lock));

    if (!list_empty (&cond->waiters))
        // -------------------------------------------------------------
        // [수정] list_pop_front: 우선순위 순으로 정렬되었으므로, 맨 앞이 최고 우선순위
        sema_up (&list_entry (list_pop_front (&cond->waiters),
                              struct semaphore_elem, elem)
                        ->semaphore);
        // -------------------------------------------------------------
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK). ... */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
    ASSERT (cond != NULL);
    ASSERT (lock != NULL);

    while (!list_empty (&cond->waiters))
        cond_signal (cond, lock);
}
