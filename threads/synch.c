/* Standard C Library headers. */
#include <debug.h>
#include <stdio.h>
#include <string.h>

/* Pintos headers. */
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Initializes semaphore S to VALUE. */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);
  list_init (&sema->waiters);
  sema->value = value;
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
      // 1.1.1. 세마포어 대기열에 FIFO 방식으로 추가
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Up or "V" operation on a semaphore. ... */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
    {
      // 1.1.2: 비효율: FIFO로 넣었기 때문에 깨울 때마다 정렬
      list_sort(&sema->waiters, thread_priority_cmp, NULL); 
        
      struct thread *t = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
      thread_unblock (t);
    }
  sema->value++;
  intr_set_level (old_level);
}

/* Tries to acquire semaphore S without blocking. */
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

/* Initializes lock L. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires a lock.  The current thread must not already hold the
   lock. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable ();

  if (lock->holder != thread_current ()) 
    {
      // ❌ 우선순위 기부 로직은 구현하지 않았다고 가정 (초보자 구현 범위)
      
      sema_down (&lock->semaphore);
      lock->holder = thread_current ();
    }

  intr_set_level (old_level);
}

/* Releases lock L.  The current thread must hold L. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable ();
  
  lock->holder = NULL;
  // ❌ 우선순위 회복 로직은 구현하지 않았다고 가정 (초보자 구현 범위)
  
  sema_up (&lock->semaphore);
  intr_set_level (old_level);
}

/* Returns true if the current thread holds lock L, false otherwise. */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

/* Initializes condition variable COND. */
void
cond_init (struct condition *cond)
{
  list_init (&cond->waiters);
}

/* 조건변수 대기열 우선순위 비교 함수 */
static bool cond_priority_cmp (const struct list_elem *a, 
                               const struct list_elem *b, 
                               void *aux UNUSED)
{
    struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
    
    // ❌ 비효율: 빈 리스트 체크 로직이 불완전할 수 있습니다.
    if (list_empty (&sa->semaphore.waiters) || list_empty (&sb->semaphore.waiters)) {
        return false;
    }
    
    struct list_elem *ta_elem = list_front (&sa->semaphore.waiters);
    struct list_elem *tb_elem = list_front (&sb->semaphore.waiters);
    
    struct thread *ta = list_entry (ta_elem, struct thread, elem);
    struct thread *tb = list_entry (tb_elem, struct thread, elem);
    
    return ta->priority > tb->priority;
}

/* Suspends execution of the current thread until condition in COND
   is signaled.  The lock LOCK must be held before calling, and will
   be released and reacquired atomically. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable ();
  waiter.thread = thread_current ();
  sema_init (&waiter.semaphore, 0);

  // 1.1.2: 조건변수 대기열에 FIFO로 추가
  list_push_back (&cond->waiters, &waiter.elem);

  lock_release (lock);
  sema_down (&waiter.semaphore);        /* Atomically block. */
  lock_acquire (lock);
  intr_set_level (old_level);
}

/* Wakes up one thread in COND's waiting list. */
void
cond_signal (struct condition *cond, struct lock *lock) 
{
  enum intr_level old_level;
  
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  old_level = intr_disable ();

  if (!list_empty (&cond->waiters)) {
    // 1.1.2: 비효율: FIFO로 넣었기 때문에 깨울 때마다 정렬
    list_sort (&cond->waiters, cond_priority_cmp, NULL); 
      
    struct semaphore_elem *waiter = list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem);
    sema_up (&waiter->semaphore);
  }

  intr_set_level (old_level);
}

/* Wakes up all threads in COND's waiting list. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  enum intr_level old_level;
  
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  old_level = intr_disable ();
  while (!list_empty (&cond->waiters))
    {
      struct semaphore_elem *waiter = list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem);
      sema_up (&waiter->semaphore);
    }
  intr_set_level (old_level);
}
