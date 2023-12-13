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
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixedpoint.h"

#include "intrinsic.h"

#ifdef USERPROG
#include "userprog/process.h"
#include "filesys/file.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

// blocked thread list 
static struct list sleep_list;
/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
bool cmp_priority (const struct list_elem *a,const struct list_elem *b, void *aux);

#define f 16384

int cnt = 0;

typedef int32_t fixedpoint;

fixedpoint convert_itof(int n){
    return n * f;
}
//음수 부분 버림
fixedpoint convert_ftoi(fixedpoint x){
    return x / f;
}
// 반올림?
fixedpoint convert_ftoi_rounding(fixedpoint x){
    if (x >= 0) return (x + (f/2))/f;
    else return (x - (f/2))/f;
}

fixedpoint fp_add(fixedpoint x, fixedpoint y){
    return x + y;
}

fixedpoint fp_subtract(fixedpoint x, fixedpoint y){
    return x - y;
}

fixedpoint fp_add_complex(fixedpoint x,int n){
    return x + n * f;
}

fixedpoint fp_subtract_complex(fixedpoint x, int n){
    return x - n * f;
}

fixedpoint fp_multiply(fixedpoint x,fixedpoint y){
    return ((int64_t) x) * y / f;
}

fixedpoint fp_multiply_complex(fixedpoint x, int n){
    return x * n;
}

fixedpoint fp_divide(fixedpoint x, fixedpoint y){
    return ((int64_t) x) * f / y;
}

fixedpoint fp_divide_complex(fixedpoint x,int n){
    return x / n;
}

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);
 
	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);
	list_init (&all_list);

	if(thread_mlfqs){
		load_avg = 0;
	}

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
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
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {

	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	//강철구
	list_push_back(&thread_current()->child_list, &t->c_elem);
	t->parent = thread_current();

	thread_unblock(t);
	thread_preemption();

	return tid;
}

void
thread_preemption(){
	if(!list_empty(&ready_list) && !intr_context()){
		struct thread *a = thread_current();
		struct list_elem *b_elem = list_begin(&ready_list);
		struct thread *b = list_entry (b_elem, struct thread, elem);

		if(a->priority < b->priority) thread_yield();
	}
}



/*  현재 스레드의 상태를 블락으로 바꾸고 
	슬립 리스트에 넣고
	깨어날 시간을 알아야 한다.  */
void
thread_sleep (int sleep_ticks) {
	struct thread *curr = thread_current();
	enum intr_level old_level;
	// 받은 매개변수 sleep_ticks와 전체 timer_tick 가 같을 때 깨워야 한다.

	// 이 동작중 인터럽트를 막는다.
	ASSERT (!intr_context ());
	old_level = intr_disable ();
	// 현재 스레드의 상태를 블락으로 바꾼다

	// 슬립리스트에 넣는다 list_push_back을 사용하면 될것같다.
	if (curr != idle_thread) {
		curr->awake_ticks = sleep_ticks;
		list_push_back (&sleep_list, &curr->elem);
		thread_block();
	}

	intr_set_level (old_level);
}
/*
	sleep_list 에 있는 쓰레드들 중에 awake ticks 가 total ticks 보다 작은 경우 깨운다.
*/
void thread_wakeup(int total_ticks) {

	enum intr_level old_level;
	struct list_elem *ptr = list_begin(&sleep_list);
	struct thread *waken;
	// 이 동작중 인터럽트를 막는다.
	old_level = intr_disable ();	

	while(ptr != list_end (&sleep_list)){
		waken = list_entry (ptr, struct thread, elem);
		if (waken->awake_ticks <= total_ticks) {
			ptr = list_remove(&waken->elem);
			thread_unblock(waken);
		} else {
			ptr = list_next(&waken->elem);	
		}
	}
	intr_set_level (old_level);	
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));
	
	old_level = intr_disable ();
	ASSERT (t->status = THREAD_BLOCKED);
	// list_push_back (&ready_list, &t->elem)
	list_insert_ordered(&ready_list, &t->elem, cmp_priority , NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// 현재 실행 중인 스레드를 CPU에서 양보하고 현재 스레드를 스케줄러에 다시 예약할수 있도록 한다.
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;
	ASSERT (!intr_context ());

	old_level = intr_disable ();

	if (curr != idle_thread)
		list_insert_ordered (&ready_list, &curr->elem, cmp_priority, NULL); 
	do_schedule(THREAD_READY);

	intr_set_level (old_level);

}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if(!thread_mlfqs){
		thread_current ()->origin_priority = new_priority;

		select_maximum_donation(thread_current());

		thread_preemption();
	}
}

void select_maximum_donation(struct thread *curr) {

	curr->priority = curr->origin_priority;

	if(!list_empty(&curr->donated_threads)) {

		struct thread *tmp = list_entry(
			list_max(&curr->donated_threads,cmp_priority_in_donate,NULL), 
				struct thread, d_elem);

		if (tmp->priority > curr->priority) curr->priority = tmp->priority;

		list_sort(&ready_list, cmp_priority, NULL);
   	}
}
/* Returns the current thread's priority. */
// 현재 스레드의 우선순위를 반환합니다. 
// 우선 순위 기부가 있는 경우 더 높은 기부된) 우선순위를 반환합니다.
int
thread_get_priority (void) {
	int result = thread_current ()->priority;
	return result;
}

// 나이스 우선순위 레디스레드는 정수
// 로드에버리지랑 리센트 시피유는 실수(고정소수점)
/* Sets the current thread's nice value to NICE. */

// - 현재 스레드의  nice 값을 새로운 nice 값으로 설정합니다.
// - 스레드의 우선 순위를 새 값에 기반하여 다시 계산합니다.
// - 실행 중인 스레드가 더 이상 가장 높은 우선 순위를 가지고 있지 않다면, 스레드는 양보합니다.
void
thread_set_nice (int nice UNUSED) {
	thread_current()->nice_point = nice;
	thread_current()->priority = calculate_priority(thread_current());
	thread_preemption();
}

/* 현재 스레드의 나이스 값 반환 */
int
thread_get_nice (void) {
	return thread_current()->nice_point;
}

fixedpoint calculate_ad_avg() {
	int ready_threads;

	if (thread_current() != idle_thread){
		ready_threads = list_size(&ready_list)+1;

	} else {
		ready_threads = list_size(&ready_list);
	}

	fixedpoint p1 = fp_divide_complex(fp_multiply_complex(load_avg, 59),60);
	fixedpoint p2 = fp_divide_complex(convert_itof(ready_threads),60);

	fixedpoint result = fp_add(p1, p2);
	return result;
}

// 현재 시스템 로드 평균을 100배 한뒤 가장 가까운 정수로 반올림 하여 반환합니다.
/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {	
	fixedpoint result = fp_multiply_complex(load_avg,100);
	return convert_ftoi_rounding(result);
}

/* Returns 100 times the current thread's recent_cpu value. */
// 각 프로세스가 '최근에' 얼마나 많은 CPU 시간을 받았는지 측정하려고 합니다. 
// 더불어, 더 최근에 받은 CPU 시간이 덜 최근에 받은 CPU 시간보다 더 무거운 가중치를 가지도록 하고자 합니다. 
// 일반적인 형태를 가진 지수 가중 이동 평균을 사용하여 이를 수행합니다.
// recent_cpu = (2*load_avg/(2*load_avg+1))*recent_cpu + nice
// load_avg 는 실행 준비 중인 스레드 수의 지수 가중 평균입니다.
// load_avg 가 1 이면, 평균적으로 CPU를 경쟁하는 스레드가 하나인 경우이므로 현재의 recent cpu 값은 log2/3(0.1) 또는 6 초에 대한 가중치로 감소합니다.
// load_avg가 2이면 가중치 0.1 에 대한 감소가 log3/4(0.1) 또는 약 8초에 대한 것입니다. 이렇게 함으로써 recent cpu는 스레드가 최근에 받은 cpu시간의 양을 추정하며,
// 감쇠 속도는 cpu 를 경쟁하는 스레드의 수에 반비례하게 됩니다.
int
thread_get_recent_cpu (void) {
	fixedpoint result = fp_multiply_complex(thread_current()->recent_cpu_point,100);

	return convert_ftoi_rounding(result);
}

void increase_recent_cpu_point() {

	if(thread_current() != idle_thread){
		thread_current()->recent_cpu_point = fp_add_complex(thread_current()->recent_cpu_point,1);
	}
}

void refresh_all_thread_recent_cpu () {
	struct list_elem *ptr = list_begin(&ready_list);

	thread_current()->recent_cpu_point = calculate_recent_cpu(thread_current());

	while(ptr != list_end(&ready_list)) {
		struct thread *curr = list_entry(ptr, struct thread, elem);
		if(curr != idle_thread) {
			curr->recent_cpu_point = calculate_recent_cpu(curr);
		}
		ptr = list_next(ptr);
	}

	ptr = list_begin(&sleep_list);

	while(ptr != list_end(&sleep_list)) {
		struct thread *curr = list_entry(ptr, struct thread, elem);
		if(curr != idle_thread) {
			curr->recent_cpu_point = calculate_recent_cpu(curr);
		}
		ptr = list_next(ptr);
	}
}

fixedpoint calculate_recent_cpu(struct thread *curr) {
	fixedpoint f_recent_cpu = curr->recent_cpu_point;

	fixedpoint double_load_avg = fp_multiply_complex(load_avg,2);

	fixedpoint decay = fp_divide(double_load_avg,fp_add_complex(double_load_avg,1));

	fixedpoint result = fp_add_complex(fp_multiply(decay,f_recent_cpu),curr->nice_point);

	return result;
}

void refresh_all_thread_priority () {
	struct list_elem *ptr = list_begin(&ready_list);

	thread_current()->priority = calculate_priority(thread_current());

	while(ptr != list_end(&ready_list)) {
		struct thread *curr = list_entry(ptr, struct thread, elem);
		if(curr != idle_thread) {
			curr->priority = calculate_priority(curr);
		}
		ptr = list_next(ptr);
	}
	
	ptr = list_begin(&sleep_list);

	while(ptr != list_end(&sleep_list)) {
		struct thread *curr = list_entry(ptr, struct thread, elem);
		if(curr != idle_thread) {
			curr->priority = calculate_priority(curr);
		}
		ptr = list_next(ptr);
	}
}

// priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
int calculate_priority(struct thread *curr){

	fixedpoint f_PRI_MAX = convert_itof(PRI_MAX);
	fixedpoint f_recent_cpu = curr->recent_cpu_point;
	fixedpoint f_nice = convert_itof(curr->nice_point);

	fixedpoint f_result = f_PRI_MAX - fp_divide_complex(f_recent_cpu,4)
								 - fp_multiply_complex(f_nice,2);

	return convert_ftoi_rounding(f_result);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	// additonal values
	t->origin_priority = priority;
	list_init(&t->donated_threads);

	// advanced scheduler
	t->nice_point = 0;
	t->recent_cpu_point = 0;

	// userprog
	t->sys_stat = 0;
	t->next_fd = NULL;

	t->parent = NULL;
	t->current_file = NULL;
	
	sema_init(&t->fork_sema,0);
	sema_init(&t->wait_sema,0);
	sema_init(&t->exit_sema,0);

	list_init(&t->child_list);
	list_init(&t->file_list);

	// lock_init(&t->child_lock);

	list_push_back(&all_list, &t->a_elem);

	for(int i = 0; i < 64; i++){
		t->file_descripter_table[i] = NULL;
	}

}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

bool cmp_priority (const struct list_elem *a,const struct list_elem *b, void *aux UNUSED){
	
	ASSERT(a != NULL);
	ASSERT(b != NULL);
	struct thread *a_entry = list_entry (a, struct thread, elem);
	struct thread *b_entry = list_entry (b, struct thread, elem);

	if(a_entry->priority > b_entry->priority) {
		return true;
	} else {
		return false;
	}
}

struct thread *find_thread_for_tid(int tid){
	struct list_elem *ptr = list_begin(&all_list);
	while(ptr != list_end(&all_list)) {
		struct thread *curr = list_entry(ptr, struct thread, a_elem);
		if(curr->tid == tid) {
			return curr;
		}
		ptr = list_next(ptr);
	}
}

struct thread *find_child_for_tid(int tid){
	struct list_elem *ptr = list_begin(&thread_current()->child_list);
	struct thread *result;
	int count = 0;
	while(ptr != list_end(&thread_current()->child_list)) {
		struct thread *curr = list_entry(ptr, struct thread, c_elem);
		if(curr->tid == tid) {
			result = curr;
			count++;
		}
		ptr = list_next(ptr);
	}
	if(count == 1){
		return result;
	} else {
		return NULL;
	}
}

