#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

bool syscall_create(char *file, unsigned initial_size);

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	switch(f->R.rax) {

	case SYS_HALT :
		syscall_halt();
		break;
	case SYS_EXIT :  
		syscall_exit(f->R.rdi);
		break;
	case SYS_FORK :
	 	f->R.rax = syscall_fork(f->R.rdi, f);
		break;
	case SYS_EXEC :

	case SYS_WAIT :

	case SYS_CREATE :
		f->R.rax = syscall_create(f->R.rdi, f->R.rsi);
		break;

	case SYS_REMOVE :

	case SYS_OPEN :

	case SYS_FILESIZE :

	case SYS_READ :

	case SYS_WRITE :
		syscall_write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	}
}

void syscall_exit (int status)
{
    struct thread *curr = thread_current (); 
	curr->sys_stat = status; 
    thread_exit();
}  

void syscall_halt (void)
{
    power_off();
}

void syscall_write (int _fd, const void *buffer, unsigned size) {
	if(_fd == 1) printf("%s", buffer);
}

// pml4_for_each (parent->pml4, duplicate_pte, parent)
// %RBX, %RSP, %RBP와 %R12 - %R15 만 복사하라
tid_t syscall_fork (const char *thread_name, struct intr_frame *f) {
	struct thread *curr = thread_current();
	tid_t tid;
	struct thread *child = process_fork(thread_name , f);

	return child->tid;
}

bool syscall_create(char *file, unsigned initial_size){
	// printf("file[0] : %s\n", file);
	// printf("size of file[0] : %d\n", sizeof(file[0]));
	// printf("initial_size : %d\n", initial_size);

	// bad-pointer                  
	if(!pml4_get_page(thread_current()->pml4, file)){
		syscall_exit(-1);
		return false;
	}
	// create-empty
	// if(file[0] == NULL){
	// 	syscall_exit(-1);
	// 	return false;
	// }
	// // create long
	// if(file[0] >= 120){
	// 	return false;
	// }
	// // create-exist

	return filesys_create(file, initial_size);

}

// int
// wait (pid_t pid) {
// 	return syscall1 (SYS_WAIT, pid);
// }
// bool
// create (const char *file, unsigned initial_size) {
// 	return syscall2 (SYS_CREATE, file, initial_size);
// }