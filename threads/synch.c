/* Standard C Library headers. */
#include <stdio.h>

/* Pintos headers. */
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/interrupt.h"

/* ... (lock_init, lock_acquire 등 함수 생략) ... */

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
   is signaled. ... */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  // ... (기본 로직 생략) ...
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

// ... (cond_broadcast 함수 생략) ...
