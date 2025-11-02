#ifndef THREAD_THREAD_H
#define THREAD_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to happen. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread's kernel stack is contained within the page
   too; typically, the struct thread itself occupies only a small
   part of the page.

   The first member is the all-threads list element, a
   struct list_elem, which must be at the beginning of struct thread.
   It is used in thread.c, thread_get_priority, etc.

   The last member is the magic number, which is used to detect
   stack overflow.  (The magic number is stored at the bottom of
   the thread's stack.)  It must be the last member of struct thread.
   When the thread's stack overflows, the magic number will be
   overwritten, and we can detect that in thread_check_stack. */

struct thread
  {
    /* Owned by thread.c. */
    struct list_elem elem;              /* List element. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    
    // -- Project 1: 추가된 필드 시작 --
    int age;                            /* 에이징용 나이 (1.2 에이징) */

    // Simplified MLFQS 용 필드 (1.3 Simplified MLFQS)
    int queue_level;                    /* MLFQS 큐 레벨 (0, 1, 2) */
    int time_slice_used;                /* 현재 타임 슬라이스에서 사용한 틱 수 */
    // -- Project 1: 추가된 필드 끝 --

    struct lock *wait_on_lock;          /* (우선순위 기부 시 사용) 현재 기다리는 lock */
    struct list donations;              /* (우선순위 기부 시 사용) 나에게 기부한 스레드 리스트 */
    struct list_elem donation_elem;     /* (우선순위 기부 시 사용) donations 리스트를 위한 elem */

    /* Shared between thread.c and synch.c. */
    struct list_elem sleep_elem;        /* sleep_list용 elem */
    int64_t wakeup_tick;                /* 깨어나야 할 시각 */

    /* Owned by userprog/process.c. */
#ifdef USERPROG
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use the multi-level feedback queue scheduler (MLFQS). */
extern bool thread_mlfqs;

// -- Project 1: 함수 선언 시작 --

// 스케줄링 비교 함수 (1.1 선점형 우선순위)
bool thread_priority_cmp (const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED);

// Simplified MLFQS 헬퍼 함수 선언 (1.3 Simplified MLFQS)
int mlfqs_get_time_slice(int queue_level);
int mlfqs_queue_to_priority(int queue_level);
void mlfqs_add_to_queue(struct thread *t);

// -- Project 1: 함수 선언 끝 --

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

#endif /* threads/thread.h */
