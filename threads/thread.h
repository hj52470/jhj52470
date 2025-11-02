#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <inttypes.h>
#include "threads/synch.h" // struct lock 정의가 필요하므로 synch.h 포함

/* States in a thread's life cycle. */
enum thread_status
{
    THREAD_RUNNING, /* Running thread. */
    THREAD_READY,   /* Not running but ready to run. */
    THREAD_BLOCKED, /* Waiting for an event to trigger. */
    THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* A kernel thread or user process.
   ... (설명은 생략) ...
   The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;              /* Thread identifier. */
    enum thread_status status; /* Thread state. */
    char name[16];          /* Name (for debugging purposes). */
    uint8_t *stack;         /* Saved stack pointer. */
    int priority;           /* Current effective priority (may be boosted by donation). */
    
    /* -----------------------------------------------------------------
     * [추가된 멤버] Project 1: Priority Donation
     * ----------------------------------------------------------------- */
    int base_priority;          /* Base priority (original priority). */
    struct lock *wait_on_lock;  /* The lock the thread is currently waiting on. */
    struct list donations;      /* List of locks/donations received (ordered by waiter priority). */
    struct list_elem donation_elem; /* Element for the lock's donation list (used by the holder). */
    /* ----------------------------------------------------------------- */
    
    int64_t wake_tick;      /* Timer tick to wake up at (for sleeping threads) */
    struct list_elem allelem; /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem; /* List element (used in ready_list, sleep_list, semaphore/lock waiters). */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir; /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic; /* Detects stack overflow. */
};

/* -----------------------------------------------------------------
 * [수정된 멤버] Project 1: Lock Donation을 위한 멤버 추가
 * synch.h에도 정의되어야 하지만, thread.h에 synch.h가 포함되어 있어 여기에 정의합니다.
 * ----------------------------------------------------------------- */
struct lock
{
    struct thread *holder;      /* Thread holding lock (or NULL). */
    struct list waiters;        /* Waiting threads. */
    
    /* Project 1: Lock Donation Field */
    struct list_elem donation_elem; /* 락이 Holder의 donations 리스트에 들어갈 때 사용 */
};

/* -----------------------------------------------------------------
 * [수정된 멤버] Project 1: Lock Donation을 위한 멤버 추가
 * Conditioon Variable의 semaphore_elem 구조체는 synch.c에 정의되어 있습니다.
 * ----------------------------------------------------------------- */


extern struct list sleep_list;
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_sleep(int64_t ticks);
void thread_wake_up(int64_t ticks_now);

/* -----------------------------------------------------------------
 * [추가된 함수 원형] Priority Donation 및 스케줄링 유틸리티
 * ----------------------------------------------------------------- */
/* Comparison function for ready_list and synch object waiters. */
bool thread_compare_priority (const struct list_elem *a,
                              const struct list_elem *b,
                              void *aux UNUSED);

/* Priority Donation functions */
void thread_donate_priority (struct thread *donor);
void thread_remove_lock (struct lock *lock);
void thread_update_priority (struct thread *t);
/* ----------------------------------------------------------------- */


#endif /* threads/thread.h */
