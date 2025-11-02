/* threads/thread.c */

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

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of ready threads (Priority-ordered). */
static struct list ready_list;

/* List of sleeping threads (Alarm Clock). */
static struct list sleep_list;

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
    void *eip;                      /* Saved instruction pointer. */
    thread_func *function;          /* Function to start running. */
    void *aux;                      /* Auxiliary data for FUNCTION. */
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

/* Project 1 Helper Function Declarations */
// Comparator for priority queues (Higher priority comes first)
bool thread_priority_cmp (const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED);
// Comparator for sleep list (Earliest wakeup time first)
bool thread_sleep_cmp (const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED);
// Preemption check for the current running thread
void thread_test_preemption(void);
// Check for threads to wake up (for Alarm Clock)
void thread_check_wakeup(int64_t current_tick);
// Puts the current thread to sleep until WAKEUP_TICK
void thread_sleep (int64_t wakeup_tick);


/* Initializes the threading system. */
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list); // Initialize sleep list for Alarm Clock

  /* Set up a thread structure for the running code. */
  initial_thread = init_thread (name_of_current_thread, PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts. */
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

/* Called by the timer interrupt handler at each timer tick. */
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

  // 1. Alarm Clock: Check if any thread needs to be woken up
  thread_check_wakeup(timer_ticks());
  
  // 2. Preemption Check (Timer Tick Preemption)
  // This check MUST be done within the interrupt context (intr_context() == true).
  if (!thread_mlfqs) {
    // Priority Scheduling Preemption
    if (t != idle_thread && !list_empty(&ready_list)) {
      if (t->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) {
        // If the current thread's priority is lower than the highest ready thread,
        // yield on return (non-blocking yield)
        intr_yield_on_return ();
      }
    }
    // Time slicing (4 ticks)
    if (t->ticks++ >= 4) {
      t->ticks = 0;
      if (t != idle_thread)
        intr_yield_on_return ();
    }
  } 
  /* MLFQS logic would go here if enabled */
}


/* Prints thread statistics. */
void
thread_print_stats (void)
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* The idle thread. */
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

  // Allocate a page for the thread structure
  t = (struct thread *) palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return NULL;

  t->magic = THREAD_MAGIC; // Initialize magic before any access checks
  
  t->name = name;
  t->priority = priority;

  // Project 1: Initialization for Priority and Alarm Clock
  t->original_priority = priority;
  list_init (&t->donations);
  t->wait_on_lock = NULL;
  t->wakeup_tick = 0; // Alarm Clock: 0 means not sleeping
  t->ticks = 0; // For time slicing

  // Initialize stack pointer (already done by palloc_get_page(PAL_ZERO))
  // The first field of struct thread is the kernel stack pointer, which is set to the base address of the page.
  
  list_push_back (&all_list, &t->allelem);

  return t;
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION with the given AUX in
   its body. */
tid_t
thread_create (const char *name, int priority, thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (priority >= PRI_MIN && priority <= PRI_MAX);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  t = init_thread (name, priority);
  if (t == NULL) {
    palloc_free_page (t);
    return TID_ERROR;
  }
  
  tid = allocate_tid ();
  t->tid = tid;
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
  old_level = intr_disable ();
  thread_unblock (t);
  intr_set_level (old_level);
  
  // 🔴 Kernel PANIC Fix: thread_create는 스레드 컨텍스트이므로
  // 새로운 스레드가 현재 스레드보다 우선순위가 높으면 안전하게 thread_yield() 호출.
  thread_test_preemption();

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
   T must be unblocked with interrupts disabled. */
void
thread_unblock (struct thread *t)
{
  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_BLOCKED);

  if (thread_mlfqs) {
    // MLFQS (Project 1-3)
  } else {
    // Priority Scheduling: Insert based on priority (highest priority first)
    list_insert_ordered (&ready_list, &t->elem, thread_priority_cmp, NULL);
  }
  t->status = THREAD_READY;
}

/* Puts the current thread to sleep until WAKEUP_TICK. 
   The current thread must not be the idle thread. */
void
thread_sleep (int64_t wakeup_tick)
{
    struct thread *curr = thread_current();
    enum intr_level old_level;

    // 인터럽트 컨텍스트 밖에서만 호출 가능
    ASSERT (!intr_context());
    
    old_level = intr_disable();

    if (curr != idle_thread) {
        // 1. 깨어날 시간 설정
        curr->wakeup_tick = wakeup_tick;
        // 2. sleep_list에 정렬하여 삽입 (가장 빨리 깰 스레드가 맨 앞으로 오도록)
        list_insert_ordered(&sleep_list, &curr->elem, thread_sleep_cmp, NULL);
        // 3. 스레드 블록 (스케줄러 호출)
        thread_block(); 
    }

    intr_set_level(old_level);
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

  // 1. Priority Donation (original_priority update)
  curr->original_priority = new_priority;
  
  // 2. Re-evaluate effective priority (only update if it's not donated)
  // If the current priority is higher (due to donation), keep it.
  if (list_empty(&curr->donations) || curr->priority < new_priority) {
      curr->priority = new_priority;
  }
  
  // 3. Preemption Check (Safe to call from thread context)
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

  // 🔴 Kernel PANIC Fix: thread_yield는 인터럽트 컨텍스트 밖에서만 호출 가능
  ASSERT (!intr_context ());

  old_level = intr_disable ();

  if (cur != idle_thread) {
    if (thread_mlfqs) {
        // MLFQS (Project 1-3)
    } else {
        // Priority Scheduling: Insert based on priority
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
        // MLFQS (Project 1-3)
        return idle_thread; // Placeholder
    } else {
        // Priority Scheduling
        if (list_empty (&ready_list))
          return idle_thread;
        else
          // ready_list는 우선순위 순으로 정렬되어 있으므로 맨 앞을 꺼냅니다.
          return list_entry (list_pop_front (&ready_list), struct thread, elem);
    }
}

/* Completes a thread switch by activating the new thread. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = thread_current ();
  
  // ... (context switch logic) ...
  
  /* Mark us as running. */
  cur->status = THREAD_RUNNING;
  cur->ticks = 0;
  
  // ... (context switch logic) ...
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

/* -- Project 1: Helper Function Implementations -- */

/* Comparator for list_insert_ordered and list_sort.
   Returns true if thread A has a higher priority than thread B. */
bool thread_priority_cmp (const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED)
{
    struct thread *ta = list_entry (a, struct thread, elem);
    struct thread *tb = list_entry (b, struct thread, elem);
    // 내림차순 정렬 (ta의 우선순위가 tb보다 높으면 TRUE)
    return ta->priority > tb->priority;
}

/* Comparator for the sleep list (earliest wakeup time first).
   Returns true if thread A should wake up before thread B. */
bool thread_sleep_cmp (const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED)
{
    struct thread *ta = list_entry (a, struct thread, elem);
    struct thread *tb = list_entry (b, struct thread, elem);
    // 오름차순 정렬 (ta의 wakeup_tick이 tb보다 작으면 TRUE)
    return ta->wakeup_tick < tb->wakeup_tick;
}


/* Compares the current running thread's priority with the highest priority
   in the ready list. Yields if the current thread is lower.
   This function MUST be called from a thread context (!intr_context()). */
void
thread_test_preemption(void)
{
  if (intr_context() || list_empty(&ready_list))
    return;
    
  // Check against the highest priority thread in ready_list
  if (thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) {
      thread_yield();
  }
}

/* Iterates through the sleep list and wakes up threads whose wakeup_tick 
   is less than or equal to the current tick. */
void
thread_check_wakeup(int64_t current_tick)
{
    struct list_elem *e = list_begin (&sleep_list);
    
    // 반복하면서 wakeup_tick을 확인
    while (e != list_end (&sleep_list))
    {
        struct thread *t = list_entry (e, struct thread, elem);

        if (t->wakeup_tick <= current_tick)
        {
            // 시간이 되었으므로 sleep_list에서 제거하고 unblock
            e = list_remove (e);
            thread_unblock (t);
        }
        else
        {
            // sleep_list가 wakeup_tick 오름차순으로 정렬되어 있다면, 
            // 현재 스레드가 시간이 안 되었다면 뒤의 스레드도 시간이 안 되었을 것이므로 반복을 멈춥니다.
            break; 
        }
    }
}
