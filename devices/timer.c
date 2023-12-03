#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
// TIMER_FREQ 가 특정 범위 안에 있는지 확인 
// PIT의 동작에 필요한 최소 및 최대 주파수를 확인하는 역할을 함

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
// 운영체제 부팅 이후 경과한 타이머 틱의 수를 나타냄
static int64_t ticks;


/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
// 타이머 틱당 반복 수를 나타냄. timer_calibrate() 에 의해 초기화됨
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt; // 타이머 인터럽트를 처리하는 함수
static bool too_many_loops (unsigned loops); // 주어진 반복횟수가 많은지 확인
static void busy_wait (int64_t loops); // 주어진 반복횟수동안 busy waiting 을 수행
static void real_time_sleep (int64_t num, int32_t denom); // 실제시간으로 일정기간 동안 대기하는 작업

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	// PIT의 입력 주파수를 TIMER_FREQ로 나누어서 최종적으로 설정할 타이머 카운터의 초기값을 계산	
	// 8254 PIT은 일반적으로 1193180hz의 입력 주파수를 가짐
	// TIMER_FREQ / 2를 더해주는 이유는 반올림을 위함
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;
	printf("timer init\n");
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
// loop_per_tick 이라는 변수를 보정하는데 사용되며 이 변수는 짧은 지연을 구현하는데 활용된다.
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  \n");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	// 타이머 틱보다 작으면서 가장 큰 2의 거듭제곱으로 loop_per_tick을 초기화
	// 1u는 unsigned 1을 의미함
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	// 좀더 정밀하게 보정하는 작업
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;
	
	// PRIu64 에서 PRI 는 print, u는 unsigned, 64는 비트수를 의미
	printf ("loops_per_tick : %'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
// 운영체제 부팅 이후 타이머 틱의 수 반환

int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
// then 이후에 현재까지 경과한 시간을 타이머 틱의 수로 반환
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();
	ASSERT (intr_get_level () == INTR_ON);
	thread_sleep(start + ticks);
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer sftatistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	// 타임 인터럽트 마다 슬립 리스트에서 시간이 다된 스레드들을 깨우고
	// 그걸 레디리스트에 넣는다.
	ticks++;
	thread_tick ();
	
	if(thread_mlfqs){
		increase_recent_cpu_point();
		// 100 틱마다 load_avg 계산
		if(timer_ticks() % TIMER_FREQ == 0){
			load_avg = calculate_ad_avg();

			refresh_all_thread_recent_cpu ();
		}
		// 4틱마다 priority 계산
		if(timer_ticks() % 4 == 0){
			refresh_all_thread_priority();
		}
	}
	thread_wakeup(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
// 
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
