#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "devices/timer.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
    void *eip;           /* Return address. */
    thread_func *function; /* Function to call. */
    void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Project 1: Priority Donation Helper Function Declarations (Definitions below) */
void thread_donate_priority (struct thread *donor);
void thread_update_priority (struct thread *t);
void thread_remove_lock (struct lock *lock);

/* -------------------------------------------------------------
 * 우선순위 비교 함수 (list_insert_ordered 사용을 위해)
 * ------------------------------------------------------------- */
bool
thread_compare_priority (const struct list_elem *a,
                         const struct list_elem *b,
                         void *aux UNUSED)
{
    return list_entry(a, struct thread, elem)->priority >
           list_entry(b, struct thread, elem)->priority;
}
/* ------------------------------------------------------------- */


/* Initializes the threading system by transforming the code
   that's currently running into a thread. ... */
void
thread_init (void)
{
    ASSERT (intr_get_level () == INTR_OFF);

    lock_init (&tid_lock);
    list_init (&ready_list); // Priority Ready List
    list_init (&all_list);
    list_init (&sleep_list);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
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

/* thread_sleep 및 thread_wake_up 함수 (기존 코드 유지) */
void
thread_sleep (int64_t ticks)
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (intr_get_level () == INTR_OFF);

    /* compute wake tick */
    cur->wake_tick = timer_ticks () + ticks; /* timer_ticks() from devices/timer.c */

    /* insert into sleep list ordered by wake_tick ascending */
    old_level = intr_disable ();
    struct list_elem *e;
    for (e = list_begin (&sleep_list); e != list_end (&sleep_list);
         e = list_next (e))
    {
        struct thread *t = list_entry (e, struct thread, elem);
        if (cur->wake_tick < t->wake_tick) {
            list_insert (e, &cur->elem);
            break;
        }
    }
    if (list_end(&sleep_list) == e)
        list_push_back (&sleep_list, &cur->elem);

    cur->status = THREAD_BLOCKED;
    schedule ();
    intr_set_level (old_level);
}
/* Wake up threads whose wake_tick <= current ticks. Called from timer interrupt. */
void
thread_wake_up (int64_t current_tick)
{
    while (!list_empty (&sleep_list))
    {
        struct list_elem *e = list_front (&sleep_list);
        struct thread *t = list_entry (e, struct thread, elem);
        if (t->wake_tick <= current_tick)
        {
            list_pop_front (&sleep_list);
            thread_unblock (t);
        }
        else
            break;
    }
}


/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick(void)
{
    struct thread *t = thread_current();

    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* -------------------------------------------------------------
     * [수정/추가] 선점형 스케줄링 로직
     * ------------------------------------------------------------- */
    // 1. time slice 만료 시 선점 유도 (Round Robin)
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return();

    // 2. Ready List의 최고 우선순위와 현재 스레드 비교 (Preemption)
    if (!list_empty(&ready_list)) {
        struct thread *highest_ready = list_entry(list_front(&ready_list), struct thread, elem);
        if (highest_ready->priority > thread_current()->priority) {
            intr_yield_on_return();
        }
    }
    /* ------------------------------------------------------------- */

    thread_wake_up(timer_ticks());
}

/* Prints thread statistics. */
void
thread_print_stats (void)
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue. ... */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;
    enum intr_level old_level;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();

    /* Prepare thread for first run by initializing its stack. ... */
    old_level = intr_disable ();

    /* Stack frame for kernel_thread(). */
    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void))kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    intr_set_level (old_level);

    /* Add to run queue. */
    thread_unblock (t);

    /* -------------------------------------------------------------
     * [추가] 새로 생성된 스레드가 현재 스레드보다 우선순위가 높으면 yield
     * ------------------------------------------------------------- */
    if (t->priority > thread_current()->priority) {
        thread_yield();
    }
    /* ------------------------------------------------------------- */

    return tid;
}

/* Puts the current thread to sleep. ... */
void
thread_block (void)
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);

    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state. ... */
void
thread_unblock (struct thread *t)
{
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);
    
    /* -------------------------------------------------------------
     * [수정] list_push_back 대신 list_insert_ordered 사용 (우선순위 스케줄링)
     * ------------------------------------------------------------- */
    list_insert_ordered (&ready_list, &t->elem, thread_compare_priority, NULL);
    /* ------------------------------------------------------------- */
    
    t->status = THREAD_READY;
    
    /* -------------------------------------------------------------
     * [추가] 차단 해제된 스레드의 우선순위가 현재 스레드보다 높으면 선점
     * ------------------------------------------------------------- */
    if (t->priority > thread_current()->priority) {
        thread_yield();
    }
    /* ------------------------------------------------------------- */
    
    intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
    return thread_current ()->name;
}

/* Returns the running thread. ... */
struct thread *
thread_current (void)
{
    struct thread *t = running_thread ();

    /* Make sure T is really a thread. ... */
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it. ... */
void
thread_exit (void)
{
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    /* Remove thread from all threads list, set our status to dying,
       and schedule another process. ... */
    intr_disable ();
    list_remove (&thread_current ()->allelem);
    thread_current ()->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}

/* Yields the CPU. ... */
void
thread_yield (void)
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable ();
    if (cur != idle_thread)
        /* -------------------------------------------------------------
         * [수정] list_push_back 대신 list_insert_ordered 사용 (우선순위 스케줄링)
         * ------------------------------------------------------------- */
        list_insert_ordered (&ready_list, &cur->elem, thread_compare_priority, NULL);
        /* ------------------------------------------------------------- */
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'. ... */
void
thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT (intr_get_level () == INTR_OFF);

    for (e = list_begin (&all_list); e != list_end (&all_list);
         e = list_next (e))
        {
            struct thread *t = list_entry (e, struct thread, allelem);
            func (t, aux);
        }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
    struct thread *cur = thread_current();
    
    /* -------------------------------------------------------------
     * [수정] Priority Donation 로직 반영: base_priority를 변경하고 priority를 재계산
     * ------------------------------------------------------------- */
    int old_priority = cur->priority;
    cur->base_priority = new_priority;
    
    // 락을 보유하고 있거나, 우선순위가 변경되었다면 update_priority 호출
    if (!list_empty(&cur->donations) || old_priority != new_priority) {
        thread_update_priority(cur);
    }
    
    // 우선순위가 낮아지거나, Ready List에 더 높은 우선순위 스레드가 있다면 yield
    if (!list_empty(&ready_list)) {
        struct thread *highest_ready = list_entry(list_front(&ready_list), struct thread, elem);
        if (highest_ready->priority > cur->priority) {
            thread_yield();
        }
    }
    /* ------------------------------------------------------------- */
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
    /* -------------------------------------------------------------
     * [수정] Donation이 반영된 effective priority를 반환
     * ------------------------------------------------------------- */
    return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
    /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
    /* Not yet implemented. */
    return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
    /* Not yet implemented. */
    return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
    /* Not yet implemented. */
    return 0;
}

/* Idle thread. ... */
static void
idle (void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;)
        {
            /* Let someone else run. */
            intr_disable ();
            thread_block ();

            /* Re-enable interrupts and wait for the next one. ... */
            asm volatile ("sti; hlt" : : : "memory");
        }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
    ASSERT (function != NULL);

    intr_enable (); /* The scheduler runs with interrupts off. */
    function (aux); /* Execute the thread function. */
    thread_exit (); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
    uint32_t *esp;

    /* Copy the CPU's stack pointer into `esp', and then round that
       down to the start of a page. ... */
    asm ("mov %%esp, %0" : "=g"(esp));
    return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *)t + PGSIZE;
    
    /* -------------------------------------------------------------
     * [수정/추가] Priority Donation 관련 멤버 초기화
     * ------------------------------------------------------------- */
    t->base_priority = priority;
    t->priority = priority;
    t->wait_on_lock = NULL; // 초기에는 기다리는 락이 없음
    list_init(&t->donations); // 기부받는 락 리스트 초기화
    /* ------------------------------------------------------------- */
    
    t->magic = THREAD_MAGIC;
    list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
    /* Stack data is always allocated in word-size units. */
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to be scheduled. ... */
static struct thread *
next_thread_to_run (void)
{
    if (list_empty (&ready_list))
        return idle_thread;
    else
        // Ready list는 우선순위 순으로 정렬되어 있으므로, 맨 앞의 스레드를 꺼냄
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it. ... */
void
thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();

    ASSERT (intr_get_level () == INTR_OFF);

    /* Mark us as running. */
    cur->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate ();
#endif

    /* If the thread we switched from is dying, destroy its struct
       thread. ... */
    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
        {
            ASSERT (prev != cur);
            palloc_free_page (prev);
        }
}

/* Schedules a new process. ... */
static void
schedule (void)
{
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    if (cur != next)
        prev = switch_threads (cur, next);
    thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
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

/* Offset of `stack' member within `struct thread'. ... */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


/* -------------------------------------------------------------
 * [Priority Donation Helper Functions]
 * ------------------------------------------------------------- */

/*
 * 현재 스레드 T가 lock을 기다릴 때, lock을 가진 스레드에게 자신의 우선순위를 기부합니다.
 * 이 함수는 재귀적으로 호출되어 기부를 전파합니다.
 */
void
thread_donate_priority (struct thread *donor)
{
    // donor는 현재 lock을 기다리는 스레드
    if (donor->wait_on_lock == NULL) return; 

    struct lock *lock = donor->wait_on_lock;
    struct thread *holder = lock->holder;
    if (holder == NULL) return; 

    // Donor의 우선순위가 Holder의 현재 우선순위보다 높을 경우에만 기부
    if (donor->priority > holder->priority) {
        holder->priority = donor->priority; 
        
        // Donation 체인 전파
        thread_donate_priority(holder);
    }
}

/*
 * 스레드가 lock을 해제할 때, T의 donations 리스트에서 해당 lock을 제거하고
 * 우선순위를 재계산합니다.
 */
void
thread_remove_lock (struct lock *lock)
{
    struct thread *cur = thread_current();
    struct list_elem *e = list_begin(&cur->donations);

    // donations 리스트에서 해당 lock을 찾아서 제거
    while (e != list_end(&cur->donations)) {
        struct lock *l = list_entry(e, struct lock, donation_elem);
        
        if (l == lock) {
            e = list_remove(e); // 제거
            break;  
        }
        e = list_next(e);
    }

    // 우선순위를 재계산합니다.
    thread_update_priority(cur);
}

/*
 * 주어진 스레드 T의 현재 유효 우선순위(effective priority)를 업데이트합니다.
 * priority는 base_priority와 donations 리스트에 있는 모든 락의 waiters 중 최고 우선순위 중
 * 가장 높은 값으로 설정됩니다.
 */
void
thread_update_priority (struct thread *t)
{
    int max_priority = t->base_priority;

    if (!list_empty(&t->donations)) {
        // donations 리스트에 있는 락들의 waiters 중 최고 우선순위를 찾습니다.
        struct list_elem *e;
        
        // donations 리스트는 우선순위 순으로 정렬되어 있으므로, 맨 앞 락의 waiters를 확인하면 됨
        struct lock *l = list_entry(list_front(&t->donations), struct lock, donation_elem);
        
        // Lock의 waiters 리스트가 우선순위 순으로 정렬되어 있으므로, 맨 앞의 스레드를 확인
        if (!list_empty(&l->waiters)) {
            struct thread *highest_waiter = list_entry(list_front(&l->waiters), struct thread, elem);
            if (highest_waiter->priority > max_priority) {
                max_priority = highest_waiter->priority;
            }
        }
    }
    
    // 우선순위가 실제로 변경되었을 경우 업데이트
    if (t->priority != max_priority) {
        t->priority = max_priority;

        // T가 READY 상태라면, Ready List의 정렬을 위해 재삽입
        if (t->status == THREAD_READY) {
            enum intr_level old_level = intr_disable();
            list_remove(&t->elem);
            list_insert_ordered(&ready_list, &t->elem, thread_compare_priority, NULL);
            intr_set_level(old_level);
        }

        // T가 다른 락을 기다리고 있다면, 변경된 우선순위를 락 보유자에게 기부 전파
        if (t->wait_on_lock != NULL) {
            thread_donate_priority(t);
        }
    }
}
