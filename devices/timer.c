/* Standard C Library headers. */
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Pintos headers. */
#include "devices/timer.h"
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* The number of timer ticks per second. */
#define TIMER_FREQ 100

/* Global list of sleeping threads (1.1.3: timer_sleep 구현) */
static struct list sleep_list;

/* Current number of timer ticks. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
  list_init(&sleep_list); // sleep_list 초기화
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, low_bit;

  loops_per_tick = 1U << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
    }

  high_bit = loops_per_tick;
  while (high_bit > 0) 
    {
      low_bit = high_bit / 2;
      if (!too_many_loops (low_bit + high_bit / 2))
        {
          loops_per_tick = low_bit + high_bit / 2;
        }
      high_bit /= 2;
    }
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* Sleeps for TICKS timer ticks.  The thread is awakened not any
   earlier than after TICKS ticks have elapsed. */
void
timer_sleep (int64_t ticks)
{
  int64_t start = timer_ticks ();

  ASSERT (intr_get_level () == INTR_ON);
  
  if (ticks > 0) {
    enum intr_level old_level = intr_disable();
    
    struct thread *curr = thread_current();
    curr->wakeup_tick = start + ticks;

    // ❌ 비효율: Sleep List에 우선순위 순이 아닌 FIFO 방식으로 넣습니다.
    list_push_back(&sleep_list, &curr->sleep_elem);

    thread_block();
    intr_set_level(old_level);
  }
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Busy-waits for approximately US microseconds. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Busy-waits for approximately NS nanoseconds. */
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick ();
  
  // 1.1.3: timer_sleep 구현 (비효율: 매 틱마다 리스트 전체 순회)
  if (!list_empty(&sleep_list)) {
      struct list_elem *e = list_begin(&sleep_list);
      
      // ❌ 비효율: 리스트 순회 중 요소를 제거하므로 반복자 관리가 까다롭습니다.
      while (e != list_end(&sleep_list)) {
          struct thread *t = list_entry(e, struct thread, sleep_elem);
          
          if (t->wakeup_tick <= ticks) {
              e = list_remove(e);
              thread_unblock(t);
          } else {
              e = list_next(e);
          }
      }
  }
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  int64_t start = ticks;
  busy_wait (loops * 2);
  return ticks > start;
}

/* Sleeps for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
     This is accurate unless the denominator is larger than TICK_FREQ.
     (In that case, we round down to 0 ticks.) */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    timer_sleep (ticks);
  else
    timer_mdelay (1); 
}

/* Busy-waits for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom) 
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom > 0);
  while (denom >= 1000 && num > 0 && num < INT64_MAX / 1000)
    {
      denom /= 1000;
      num /= 1000;
    }

  busy_wait (loops_per_tick * num / denom);
}

/* Simply calls 'asm volatile ("nop")' many times.
   Ideally, each call should take one clock cycle. */
static void
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    asm volatile ("nop");
}
