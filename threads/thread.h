#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <inttypes.h>
#include "threads/synch.h"
#include "threads/fixed-point.h" // Fixed-Point Arithmetic 포함

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
   ... */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;              /* Thread identifier. */
    enum thread_status status; /* Thread state. */
    char name[16];          /* Name (for debugging purposes). */
    uint8_t *stack;         /* Saved stack pointer. */
    int priority;           /* Current effective priority (may be boosted by donation). */
    
    /* -----------------------------------------------------------------
     * [Project 1: Priority Donation]
     * ----------------------------------------------------------------- */
    int base_priority;          /* Base priority (original priority). */
    struct lock *wait_on_lock;  /* The lock the thread is currently waiting on. */
    struct list donations;      /* List of locks/donations received (ordered by waiter priority). */
    struct list_elem donation_elem; /* Element for the lock's donation list (used by the holder). */
    
    /* -----------------------------------------------------------------
     * [Project 1: MLFQS]
     * ----------------------------------------------------------------- */
    int nice;                   /* Nice value. Default 0. */
    fixed_t recent_cpu;         /* Recent CPU usage, in fixed-point. */
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
 * [Project 1: Lock Structure Addition] (synch.h에 포함됨)
 * ----------------------------------------------------------------- */
struct lock
{
    struct thread *holder;      /* Thread holding lock (or NULL). */
    struct list waiters;        /* Waiting threads (priority ordered). */
    
    /* Project 1: Lock Donation Field */
    struct list_elem donation_elem; /* 락이 Holder의 donations 리스트에 들어갈 때 사용 */
};

extern struct list sleep_list;

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* -----------------------------------------------------------------
 * [Project 1: MLFQS Global Variable]
 * ----------------------------------------------------------------- */
extern fixed_t load_avg; /* System load average, in fixed-point. */
/* ----------------------------------------------------------------- */


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
 * [Project 1: Utility & Donation Function Prototypes]
 * ----------------------------------------------------------------- */
/* Comparison function for ready_list and synch object waiters. */
bool thread_compare_priority (const struct list_elem *a,
                              const struct list_elem *b,
                              void *aux UNUSED);

/* Priority Donation functions */
void thread_donate_priority (struct thread *donor);
void thread_remove_lock (struct lock *lock);
void thread_update_priority (struct thread *t);

/* MLFQS Helper functions */
void thread_calculate_priority (struct thread *t);
void thread_calculate_recent_cpu (struct thread *t);
void thread_update_recent_cpu_and_load_avg (void);
void thread_update_all_priority (void);
/* ----------------------------------------------------------------- */


#endif /* threads/thread.h */
