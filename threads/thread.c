/* Standard C Library headers. */
#include <debug.h>
#include <list.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>

/* Pintos headers. */
#include "threads/thread.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* An array of thread pointers to use in the multi-level feedback
   queue scheduler (MLFQS). */
#define MLFQS_QUEUES 3

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of ready threads. */
static struct list ready_list;

// -- Project 1: MLFQS 전역 변수 추가 --
static struct list ready_list_q0;  /* 최고 우선순위 큐 */
static struct list ready_list_q1;  /* 중간 우선순위 큐 */
static struct list ready_list_q2;  /* 최저 우선순위 큐 */
// -- Project 1: MLFQS 전역 변수 추가 끝 --

/* List of all threads. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by thread_create() to synchronize the creation of threads. */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Saved instruction pointer. */
    thread_func *function;      /* Function to start running. */
    void *aux;                  /* Auxiliary data for FUNCTION. */
  };

/* Stacks. */
#define STACK_SIZE 4096

/* Thread statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks spent in kernel threads. */
static long long user_ticks;    /* # of timer ticks spent in user processes. */

/* MLFQS on/off switch. */
bool thread_mlfqs;

/* Static function prototypes. */
static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static struct thread *init_thread (const char *name, int priority);
static void *alloc_frame (struct thread *t, size_t size);
static void schedule (void);
static struct thread *next_thread_to_run (void);
static void switch_threads (struct thread *curr, struct thread *next);
static void thread_check_stack (void);
static void thread_print_stats_aux (struct thread *t, void *aux);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread and creating the idle
   thread.

   This function should be called once early in init.c.  It
   shouldn't return until after thread_start() has been called. */
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  
  // -- Project 1: MLFQS 큐 초기화 추가 --
  list_init (&ready_list_q0);
  list_init (&ready_list_q1);
  list_init (&ready_list_q2);
  // -- Project 1: MLFQS 큐 초기화 끝 --

  /* Set up a thread structure for the running code. */
  initial_thread = init_thread (name_of_current_thread, PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Amplifies the priority of all threads in ready_list if not mlfqs. */
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update stats. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  // -- Project 1: 에이징 및 MLFQS 로직 (1.2, 1.3) --
  if (!thread_mlfqs) {
    // 1.2 에이징 처리 (비효율적: 재정렬 누락)
    struct list_elem *e;
    for (e = list_begin (&ready_list); 
         e != list_end (&ready_list); 
         e = list_next (e))
    {
        struct thread *ready_t = list_entry (e, struct thread, elem);
        ready_t->age++;
        
        if (ready_t->age >= 20 && ready_t->priority < PRI_DEFAULT) {
            ready_t->priority++;
            ready_t->age = 0;
            
            // ❌ 치명적 비효율: 우선순위가 바뀌어도 list_sort를 호출하지 않아 순서가 꼬입니다.
            // list_sort (&ready_list, thread_priority_cmp, NULL); // 이 코드가 빠짐
        }
    }
  } else {
    // 1.3 Simplified MLFQS
    if (t != idle_thread) {
        t->time_slice_used++;
        int time_slice = mlfqs_get_time_slice (t->queue_level);
        
        // 타임 슬라이스 소모 시 큐 강등
        if (t->time_slice_used >= time_slice) {
            t->time_slice_used = 0;
            if (t->queue_level < 2) {
                t->queue_level++;
                t->priority = mlfqs_queue_to_priority (t->queue_level);
            }
            intr_yield_on_return ();
        }
    }
    
    // 1.3 MLFQS: 대기 중인 스레드 에이징 및 승급
    struct list *queues[] = {&ready_list_q0, &ready_list_q1, &ready_list_q2};
    for (int q = 0; q < 3; q++) {
        struct list_elem *e;
        struct list_elem *next_e;
        
        e = list_begin (queues[q]); 
        while (e != list_end (queues[q]))
        {
            struct thread *ready_t = list_entry (e, struct thread, elem);
            ready_t->age++;
            
            // age 20 도달 시 상위 큐로 승급
            if (ready_t->age >= 20 && ready_t->queue_level > 0) {
                ready_t->age = 0;
                ready_t->queue_level--;
                ready_t->priority = mlfqs_queue_to_priority (ready_t->queue_level);
                
                // 큐 이동
                next_e = list_next (e);
                list_remove (e);
                mlfqs_add_to_queue (ready_t);
                e = next_e;
                
                // ❌ 비효율: 큐 이동 후 intr_yield_on_return()을 호출하지 않아 선점이 지연됩니다.
            } else {
                e = list_next (e);
            }
        }
    }
  }
  
  if (intr_context () && thread_current () != idle_thread) 
    intr_yield_on_return ();
}
// ----------------------------------------------------------------------


/* Prints thread statistics. */
void
thread_print_stats (void)
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* The idle thread.  Executes when no other thread is ready to run. */
static void
idle (void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;)
    {
      /* Let some other thread run and wait for the next interrupt. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and jump to the next thread. */
      asm volatile ("sti");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the thread that currently has the CPU. */
struct thread *
thread_current (void)
{
  return (struct thread *) running_thread ();
}

/* Returns the current thread's tid. */
tid_t
thread_tid (void)
{
  return thread_current ()->tid;
}

/* Returns the current thread's name. */
const char *
thread_name (void)
{
  return thread_current ()->name;
}

/* Returns true if T appears to point to a valid thread. */
bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T. */
static struct thread *
init_thread (const char *name, int priority)
{
  struct thread *t = thread_current ();
  ASSERT (t != NULL);
  t->name = name;
  t->priority = priority;

  // -- Project 1: 필드 초기화 시작 --
  t->age = 0;              
  t->original_priority = priority; 
  t->queue_level = 0;      
  t->time_slice_used = 0;  
  list_init (&t->donations);
  t->wait_on_lock = NULL;
  // -- Project 1: 필드 초기화 끝 --

  // ... (스택 포인터, 매직 넘버 등 초기화 생략) ...
  list_push_back (&all_list, &t->allelem);

  return t;
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION with the given AUX in
   its body.  (If AUX is null, it's ignored.)

   Returns the new thread's tid, or TID_ERROR if creation fails. */
tid_t
thread_create (const char *name, int priority, thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (priority >= PRI_MIN && priority <= PRI_MAX);
  
  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (name, priority);
  t->tid = allocate_tid ();
  t->status = THREAD_BLOCKED;

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);
  
  // -- Project 1: 선점 체크 (1.1 선점형 우선순위) --
  // 비효율: thread_unblock()에서 선점 체크를 놓쳤을 경우를 대비한 중복 체크
  if (thread_current ()->priority < priority) {
      thread_yield ();
  }
  // -- Project 1: 선점 체크 끝 --

  return t->tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awakened by thread_unblock(). */
void
thread_block (void)
{
  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state. */
void
thread_unblock (struct thread *t)
{
  enum intr_level old_level;
  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  t->age = 0; // 1.2 에이징: ready 큐 진입 시 age 리셋

  if (thread_mlfqs) {
    // 1.3 Simplified MLFQS
    t->priority = mlfqs_queue_to_priority (t->queue_level);
    mlfqs_add_to_queue (t);
    t->status = THREAD_READY;

    // 1.3 MLFQS: Q0 선점 체크 (비효율적인 로직)
    struct thread *cur = thread_current();
    if (t->queue_level == 0 && cur != idle_thread && cur->queue_level > 0) {
        intr_set_level (old_level); // 인터럽트 복원
        // ❌ 치명적 비효율: 컨텍스트 상관없이 무조건 thread_yield() 호출
        thread_yield(); 
        return;
    }
  } else {
    // 1.1 선점형 우선순위 스케줄링
    list_insert_ordered (&ready_list, &t->elem, thread_priority_cmp, NULL);
    t->status = THREAD_READY;
    
    // ❌ 선점 로직 누락: 여기서 thread_yield() 또는 선점 체크가 빠짐
  }
  
  intr_set_level (old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
  return thread_current ()->priority;
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
  enum intr_level old_level = intr_disable ();
  struct thread *curr = thread_current ();

  // ❌ 비효율: 우선순위 기부 시 원본 우선순위 갱신 로직이 누락될 수 있음
  curr->original_priority = new_priority;
  curr->priority = new_priority;
  
  // 1.1 선점: 우선순위가 낮아지면 양보
  thread_test_preemption(); 
  
  intr_set_level (old_level);
}

/* Yields the CPU.  The current thread is not put to sleep and may be
   scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  
  if (cur != idle_thread) {
    if (thread_mlfqs) {
        // 1.3 Simplified MLFQS
        mlfqs_add_to_queue (cur);
        cur->time_slice_used = 0;
    } else {
        // 1.1 선점형 우선순위 스케줄링
        cur->age = 0; // 1.2 에이징: ready 큐 진입 시 age 리셋
        list_insert_ordered (&ready_list, &cur->elem, thread_priority_cmp, NULL);
    }
  }

  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Deschedules the current thread and schedules a new one. */
static void
schedule (void)
{
  struct thread *cur = thread_current ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Chooses and returns the next thread to be scheduled. */
static struct thread *
next_thread_to_run (void)
{
    if (thread_mlfqs) {
        // 1.3 Simplified MLFQS: Q0 -> Q1 -> Q2 순서로 선택
        if (!list_empty(&ready_list_q0)) {
            return list_entry (list_pop_front(&ready_list_q0), struct thread, elem);
        } else if (!list_empty(&ready_list_q1)) {
            return list_entry (list_pop_front(&ready_list_q1), struct thread, elem);
        } else if (!list_empty(&ready_list_q2)) {
            return list_entry (list_pop_front(&ready_list_q2), struct thread, elem);
        } else {
            return idle_thread;
        }
    } else {
        // 1.1 선점형 우선순위 스케줄링
        if (list_empty (&ready_list))
          return idle_thread;
        else
          return list_entry (list_pop_front (&ready_list), struct thread, elem);
    }
}

/* Completes a thread switch by activating the new thread. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = thread_current ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  cur->ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct.
     This must happen now to avoid races later.
     That is, we cannot destroy it until we're sure it's not running
     on another CPU. */
  if (prev != NULL && prev->status == THREAD_DYING) 
    {
      ASSERT (prev != initial_thread);
      printf ("Thread %s terminated.\n", prev->name);
      palloc_free_page (prev);
    }
}

/* Destroys the current thread and schedules a new one. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list. */
  list_remove (&thread_current ()->allelem);

  /* Mark thread as dying and schedule a new one. */
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Checks the current thread's stack for overflow. */
static void
thread_check_stack (void)
{
  ASSERT (thread_current ()->magic == THREAD_MAGIC);
}

/* Allocates a new thread identifier. */
static tid_t
allocate_tid (void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Returns the thread that currently has the CPU. */
struct thread *
thread_current (void)
{
  return (struct thread *) running_thread ();
}

/* -- Project 1: 추가된 헬퍼 함수 구현 시작 -- */

/* 우선순위 비교 함수. 높은 우선순위가 앞. */
bool thread_priority_cmp (const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED)
{
    struct thread *ta = list_entry (a, struct thread, elem);
    struct thread *tb = list_entry (b, struct thread, elem);
    return ta->priority > tb->priority;
}

/* 현재 스레드의 우선순위와 ready_list의 최고 우선순위를 비교하여 선점 여부 결정. */
void
thread_test_preemption(void)
{
  if (!list_empty(&ready_list) && 
      thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) {
      thread_yield();
  }
}

/* MLFQS 큐 레벨에 따른 타임 슬라이스 반환 */
int mlfqs_get_time_slice(int queue_level) {
    if (queue_level == 0) return 2;
    if (queue_level == 1) return 4;
    if (queue_level == 2) return 8;
    return 8; 
}

/* MLFQS 큐 레벨에 따른 우선순위 반환 */
int mlfqs_queue_to_priority(int queue_level) {
    if (queue_level == 0) return 50;
    if (queue_level == 1) return 30;
    if (queue_level == 2) return 10;
    return 10; 
}

/* 적절한 큐에 스레드 추가 */
void mlfqs_add_to_queue(struct thread *t) {
    if (t->queue_level == 0) list_push_back(&ready_list_q0, &t->elem);
    else if (t->queue_level == 1) list_push_back(&ready_list_q1, &t->elem);
    else if (t->queue_level == 2) list_push_back(&ready_list_q2, &t->elem);
    else list_push_back(&ready_list_q2, &t->elem); 
}

/* -- Project 1: 추가된 헬퍼 함수 구현 끝 -- */

// ... (thread_foreach, thread_get_nice, thread_set_nice 등 나머지 함수 생략) ...
