#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <inttypes.h>
#include "threads/synch.h" // struct lock 사용을 위해 synch.h 포함

/* States in a thread's life cycle. (생략) */
enum thread_status
// ... (기존 코드 유지) ...

/* Thread identifier type. (생략) */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* A kernel thread or user process. (생략) */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;              /* Thread identifier. */
    enum thread_status status; /* Thread state. */
    char name[16];          /* Name (for debugging purposes). */
    uint8_t *stack;         /* Saved stack pointer. */
    
    // -----------------------------------------------------------------
    int priority;           /* Current effective priority. (실제 우선순위) */
    int base_priority;      /* Base priority (초기 또는 기부 후 복구할 우선순위) */
    struct lock *wait_on_lock; /* The lock the thread is currently waiting on. */
    struct list donations;  /* List of locks/donations received. */
    struct list_elem donation_elem; /* List element for donations list. */
    int age;                /* (Aging용) ready_list에서 대기한 틱 수 */
    // -----------------------------------------------------------------
    
    int64_t wake_tick;
    struct list_elem allelem; /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem; /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir; /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic; /* Detects stack overflow. */
};
extern struct list sleep_list;

/* If false (default), use round-robin scheduler. (생략) */
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

/* Performs some operation on thread t, given auxiliary data AUX. (생략) */
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

// -----------------------------------------------------------------
/* Comparison function for ready_list and synch object waiters. */
bool thread_compare_priority (const struct list_elem *a,
                              const struct list_elem *b,
                              void *aux UNUSED);

/* Priority Donation functions */
void thread_donate_priority (struct thread *donor);
void thread_remove_lock (struct lock *lock);
void thread_update_priority (struct thread *t);
// -----------------------------------------------------------------

#endif /* threads/thread.h */
