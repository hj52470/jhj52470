/* Standard C Library headers. */
#include <stdio.h>
#include <string.h>

/* Pintos headers. */
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

// ... (다른 전역 변수 생략)

/* List of ready threads. */
static struct list ready_list;

// -- Project 1: MLFQS 전역 변수 추가 시작 --
// 1.3 Simplified MLFQS: 3단계 피드백 큐
static struct list ready_list_q0;  /* 최고 우선순위 큐 */
static struct list ready_list_q1;  /* 중간 우선순위 큐 */
static struct list ready_list_q2;  /* 최저 우선순위 큐 */
// -- Project 1: MLFQS 전역 변수 추가 끝 --


/* List of all threads. */
static struct list all_list;

// ... (다른 정적 함수 선언 생략)

/* Initializes the threading system by transforming the code
   that's currently running into a thread and creating the idle
   thread.

   This function should be called once early in init.c.  It
   shouldn't return until after thread_start() has been called. */
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list); // timer_sleep 관련

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

/* Initializes T as an idle thread. */
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

/* Does basic initialization of T. */
static struct thread *
init_thread (const char *name, int priority)
{
  struct thread *t = thread_current ();
  ASSERT (t != NULL);
  t->name = name;
  t->priority = priority;

  // -- Project 1: 필드 초기화 시작 --
  t->age = 0;              /* 에이징 나이 초기화 (1.2 에이징) */
  t->queue_level = 0;      /* MLFQS 큐 레벨 초기화 (1.3 MLFQS) */
  t->time_slice_used = 0;  /* MLFQS 타임 슬라이스 사용량 초기화 (1.3 MLFQS) */
  // ... (우선순위 기부 관련 필드 초기화 생략) ...
  // -- Project 1: 필드 초기화 끝 --

  // ... (기타 초기화 생략) ...

  list_push_back (&all_list, &t->allelem);

  return t;
}

// -- Project 1: 헬퍼 함수 구현 시작 --

/* 우선순위 비교 함수. 높은 우선순위가 앞. (1.1 선점형 우선순위) */
bool thread_priority_cmp (const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED)
{
    struct thread *ta = list_entry (a, struct thread, elem);
    struct thread *tb = list_entry (b, struct thread, elem);
    return ta->priority > tb->priority;
}

/* MLFQS 큐 레벨에 따른 타임 슬라이스 반환 (1.3 MLFQS) */
int mlfqs_get_time_slice(int queue_level) {
    if (queue_level == 0) return 2;
    if (queue_level == 1) return 4;
    if (queue_level == 2) return 8;
    return 0; // 오류
}

/* MLFQS 큐 레벨에 따른 우선순위 반환 (1.3 MLFQS) */
int mlfqs_queue_to_priority(int queue_level) {
    if (queue_level == 0) return 50;
    if (queue_level == 1) return 30;
    if (queue_level == 2) return 10;
    return PRI_DEFAULT; // 오류
}

/* 적절한 큐에 스레드 추가 (1.3 MLFQS) */
void mlfqs_add_to_queue(struct thread *t) {
    if (t->queue_level == 0) list_push_back(&ready_list_q0, &t->elem);
    else if (t->queue_level == 1) list_push_back(&ready_list_q1, &t->elem);
    else if (t->queue_level == 2) list_push_back(&ready_list_q2, &t->elem);
}
// -- Project 1: 헬퍼 함수 구현 끝 --

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION with the given AUX in
   its body.  (If AUX is null, it's ignored.)

   Returns the new thread's tid, or TID_ERROR if creation fails. */
tid_t
thread_create (const char *name, int priority, thread_func *function, void *aux)
{
  // ... (스레드 생성 및 초기화 코드 생략) ...

  /* Add to run queue. */
  thread_unblock (t);
  
  // -- Project 1: 선점 체크 (1.1 선점형 우선순위) --
  // ❌ 초보자 실수: thread_unblock() 내에서 선점 체크가 안 됐으므로 여기서 체크합니다.
  if (thread_current ()->priority < priority) {
      thread_yield ();
  }
  // -- Project 1: 선점 체크 끝 --

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awakened by thread_unblock(). */
void
thread_block (void)
{
  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an elementary implementation of thread_unblock, as there is
   no priority donation logic. */
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
    
    // ❌ 선점 로직 누락 (초보자 실수: yield를 여기서 하지 않습니다.)
  }
  
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
        cur->time_slice_used = 0; // time_slice_used 리셋
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

/* ... (thread_exit, schedule 함수 생략) ... */

/* Chooses and returns the next thread to be scheduled.  If no thread
   is ready, returns the idle thread. */
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

/* ... (thread_check_stack 함수 생략) ... */

/* Called by the timer interrupt handler at each timer tick.
   Amplifies the priority of all threads in ready_list if not mlfqs. */
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update stats. */
  if (t != idle_thread)
    t->ticks++;

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
        for (e = list_begin (queues[q]); 
             e != list_end (queues[q]); 
             e = list_next (e))
        {
            struct thread *ready_t = list_entry (e, struct thread, elem);
            ready_t->age++;
            
            // age 20 도달 시 상위 큐로 승급
            if (ready_t->age >= 20 && ready_t->queue_level > 0) {
                ready_t->age = 0;
                ready_t->queue_level--;
                ready_t->priority = mlfqs_queue_to_priority (ready_t->queue_level);
                
                // 큐 이동
                list_remove (e);
                mlfqs_add_to_queue (ready_t);
                e = list_prev (e);
                
                // ❌ 비효율: 큐 이동 후 intr_yield_on_return()을 호출하지 않아 선점이 지연됩니다.
            }
        }
    }
  }
  
  if (intr_context () && thread_current () != idle_thread) 
    intr_yield_on_return ();
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
  enum intr_level old_level = intr_disable ();
  struct thread *curr = thread_current ();

  curr->priority = new_priority;
  
  // 1.1 선점: 우선순위가 낮아지면 양보 (높아질 때는 처리 안 함)
  thread_test_preemption(); 
  
  // ❌ 비효율: ready_list에 있는 스레드의 우선순위가 올라갔을 때의 처리는 하지 않습니다.
  // ❌ (donate 로직이 없으므로 여기서 list_sort도 하지 않습니다.)

  intr_set_level (old_level);
}

/* ... (thread_get_priority, thread_get_nice, thread_set_nice 등 생략) ... */
