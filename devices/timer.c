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

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. 
   초당 PIT_FREQ 횟수를 인터럽트하도록  
   8254 PIT(Programmable Interval Tiemr) 를 설정하고
   해당 인터럽트를 등록합니다.
   */
void timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	// PIT의 입력 주파수를 TIMER_REQ로 나누어서 최종적으로 설정할 타이머 카운터의 초기값을 계산
	// 8254 PIT은 일반적으로 1193180hz의 입력 주파수를 가짐.
	// TIMER_FREQ/2 를 더해주는 이유는 반올림을 위함
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	/*outb 내부의 outsb
		DS:(E)SI또는 RSI에 지정된 메모리 위치에서
		DX^2에 지정된 I/O 포트로 바이트를 출력합니다.*/

	//CW : (추측) Counter word*/
	/* 카운터 비트 표현 방식
		//LSB : Least Significant Bit 최소 중요 비트 
		//MSB : Most Significant Bit 최대 중요 비트
	*/
	// mode 2 : 카운터 동작 모드, 주기적 또는 비주기적으로 
	// 			값을 증가 또는 감소 시키는 방식과 관련
	// Binary : 이진법. 즉, 카운터의 값은 이진수로 표현되고 있음.
	
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	/* 디버깅 목적으로 NAME이라는 HANDLER를 호출하기 위해 
	외부 인터럽트 VEC_NO를 등록합니다. 
	핸들러는 인터럽트가 비활성화된 상태로 실행됩니다*/
	// 32~255(0x20~0xff) 까지는 커널 설계자가 정의할 수 있도록 되어 있음.
	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
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
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
void timer_sleep (int64_t ticks) {
	//ticks : 재울 시간
	int64_t start = timer_ticks();

	ASSERT (intr_get_level () == INTR_ON);
	/* 	for busy_waiting
	while (timer_elapsed (start) < ticks)
		thread_yield ();	*/
	//if(timer_elapsed(start)<ticks){
		thread_sleep(start+ticks);
	//}
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
void timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void timer_interrupt (struct intr_frame *args UNUSED) {
	// 타임 인터럽트 마다 sleep_list에서 시간이 다 된 스레드들을 깨우고
	// 그걸 ready_list에 넣는다.
	ticks++;
	thread_tick ();
	if(thread_mlfqs){
		mlfqs_increment();
		if(timer_ticks()%TIMER_FREQ==0){
			mlfqs_recent_cpu_all(thread_current());
			mlfqs_load_avg();
		}
	}
	//check the sleep list and the global tick
	thread_find_wakeup(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
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
/*
int64_t save_min_tick(int64_t cur_ticks){
	if(ticks>cur_ticks)
	ticks = cur_ticks;
}
int64_t ret_min_tick(){
	return ticks;
}*/