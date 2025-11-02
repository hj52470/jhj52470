#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore
{
    unsigned value;       /* Current value. */
    struct list waiters;  /* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock
{
    struct thread *holder;       /* Thread holding lock (for debugging). */
    // -----------------------------------------------------------------
    // [수정] Priority Donation을 위해 semaphore를 waiters 리스트로 대체
    struct list waiters;         /* List of waiting threads (ordered by priority). */
    struct list_elem donation_elem; /* Element for the holder's donations list. */
    // -----------------------------------------------------------------
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. (생략) */
struct condition
// ... (기존 코드 유지) ...

/* Optimization barrier. (생략) */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
