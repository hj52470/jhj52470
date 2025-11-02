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

// thread.c에 정의된 Priority Donation 및 비교 함수 선언
extern bool thread_compare_priority (const struct list_elem *a,
                                     const struct list_elem *b,
                                     void *aux UNUSED);
extern void thread_donate_priority (struct thread *donor);
extern void thread_update_priority (struct thread *t);
extern void thread_remove_lock (struct lock *lock);


/* Initializes semaphore SEMA to VALUE. ... */
void
sema_init (struct semaphore *sema, unsigned value)
{
    ASSERT (sema != NULL);

    sema->value = value;
    // Project 1: waiters 리스트를 우선순위 순으로 정렬되도록 초기화
    list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore. ... */
void
sema_down (struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT (sema != NULL);
    ASSERT (!intr_context ());

    old_level = intr_disable ();
    while (sema->value == 0)
        {
            // Project 1: list_push_back 대신 list_insert_ordered 사용 (우선순위 정렬)
            list_insert_ordered (&sema->waiters, &thread_current ()->elem,
                                 thread_compare_priority, NULL);
            thread_block ();
        }
    sema->value--;
    intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0. ... */
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

/* Up or "V" operation on a semaphore. ... */
void
sema_up (struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT (sema != NULL);

    old_level = intr_disable ();
    if (!list_empty (&sema->waiters)) {
        // Project 1: list_pop_front: 우선순위 순으로 정렬되었으므로, 맨 앞이 최고 우선순위
        struct thread *t = list_entry (list_pop_front (&sema->waiters),
                                       struct thread, elem);
        thread_unblock (t);

        // thread_unblock 내부에서 선점 검사를 수행함
    }
    sema->value++;
    intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads. ... */
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
    // Project 1: Lock waiters 리스트를 우선순위 기반으로 사용
    list_init (&lock->waiters);
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
        // Project 1: Priority Donation 로직

        // 1. 현재 스레드가 기다리는 락 설정
        cur->wait_on_lock = lock;

        // 2. 현재 락 보유자의 donations 리스트에 이 락을 추가하고 우선순위 기부 전파
        // donations 리스트는 락의 waiters 리스트의 최고 우선순위 순으로 정렬되어야 함.
        list_insert_ordered (&lock->holder->donations, &lock->donation_elem,
                             thread_compare_priority, NULL); // thread_compare_priority를 사용하여 정렬 삽입 (편의상)
        thread_donate_priority(cur);

        // 3. lock의 waiters 리스트에 현재 스레드 삽입 (우선순위 순 정렬)
        list_insert_ordered (&lock->waiters, &cur->elem, thread_compare_priority, NULL);

        // 4. 블록
        thread_block ();

        // 5. Block에서 깨어났으므로, 더 이상 락을 기다리지 않음
        cur->wait_on_lock = NULL;
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
    // lock_held_by_current_thread는 lock->holder가 cur인지 확인하므로
    // lock->holder가 NULL이면 항상 false를 반환
    // ASSERT (!lock_held_by_current_thread (lock)); // lock_held_by_current_thread(lock)가 true이면 ASSERT 오류.

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

    // Project 1: Priority Donation 로직

    // 1. donations 리스트에서 현재 해제하는 락 제거 및 우선순위 재계산
    // thread_remove_lock 내부에서 thread_update_priority 호출됨
    thread_remove_lock(lock);

    // 2. Lock의 waiters 리스트에서 최고 우선순위 스레드를 unblock
    if (!list_empty (&lock->waiters)) {
        struct thread *t = list_entry(list_pop_front(&lock->waiters), struct thread, elem);

        // 락 해제 후 락 보유자 변경 (unblock 전에 해야 새 스레드가 락을 획득하게 됨)
        lock->holder = t;

        thread_unblock(t);
        // thread_unblock에서 선점 검사를 수행함
    } else {
        // 기다리는 스레드가 없으면 락 보유자 해제
        lock->holder = NULL;
    }

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
    struct list_elem elem;     /* List element. */
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
    // Project 1: list_push_back 대신 list_insert_ordered 사용 (우선순위 정렬)
    list_insert_ordered (&cond->waiters, &waiter.elem, thread_compare_priority, NULL);

    lock_release (lock); // lock_release 내부에서 donation priority 회복 처리됨
    sema_down (&waiter.semaphore);
    lock_acquire (lock); // lock_acquire 내부에서 donation priority 적용 처리됨
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
        // Project 1: list_pop_front: 우선순위 순으로 정렬되었으므로, 맨 앞이 최고 우선순위
        sema_up (&list_entry (list_pop_front (&cond->waiters),
                              struct semaphore_elem, elem)
                            ->semaphore);
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
