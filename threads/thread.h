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

/* A kernel thread or user process. */
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
    
    // -- Project 1: 추가된 필드 --
    int age;                            /* 에이징용 나이 */
    int original_priority;              /* 우선순위 기부 전 원래 우선순위 (비효율: 기부 로직은 여기서 생략) */

    // Simplified MLFQS 용 필드
    int queue_level;                    /* MLFQS 큐 레벨 (0, 1, 2) */
    int time_slice_used;                /* 현재 타임 슬라이스에서 사용한 틱 수 */
    // -- Project 1: 추가된 필드 끝 --

    struct lock *wait_on_lock;
    struct list donations;
    struct list_elem donation_elem;

    /* Shared between thread.c and synch.c. */
    struct list_elem sleep_elem;        /* sleep_list용 elem (timer.c에서 사용) */
    int64_t wakeup_tick;                /* 깨어나야 할 시각 (timer.c에서 사용) */

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
bool thread_priority_cmp (const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED);

void thread_test_preemption (void); // thread_set_priority를 위해 추가

// Simplified MLFQS 헬퍼 함수 선언
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
