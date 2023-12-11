#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
bool user_create (const char *file, unsigned initial_size);
int user_open(const char *file);
bool user_remove (const char *file);

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
	// TODO: Your implementation goes here.
	switch(f->R.rax){
		case SYS_WRITE:
			user_write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_HALT:
			user_halt();
			break;
		case SYS_EXIT:
			user_exit (f->R.rdi);
			break;
		case SYS_CREATE:
		 	f->R.rax = user_create(f->R.rdi, f->R.rsi);
		 	break;
		case SYS_REMOVE:
			f->R.rax = user_remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = user_open(f->R.rdi);
			break;
	}
}

void user_write(int fd, const void *buffer, unsigned size){
	printf("%s", buffer);
}

void user_halt(void){
	power_off();
}

void user_exit(int status){
	struct thread *curr = thread_current();
	curr->exit_status = status;
	//printf("[1] thread_status : %d, exit_status : %d", curr->status, curr->exit_status);
	thread_exit();
	//printf("[5] thread_status : %d, exit_status : %d", curr->status, curr->exit_status);
}

bool user_create (const char *file, unsigned initial_size) {
	bool result;

	/* [Error] craete null */
	if(file==NULL){
		user_exit(-1);
	}

	/* [Error] bad pointer*/
	/* [Error] create empty */
	if(!pml4_get_page(thread_current()->pml4, file)){
		// printf("in ! pml get page\n");
		user_exit(-1);
	}
	/* [Error] long */
	if(strlen(file)>128){
		return false;
	}
	
	result = filesys_create(file, initial_size);

	return result;
 }

bool user_remove (const char *file){
	return filesys_remove(file);
}

int user_open(const char *file){
	struct thread *curr = thread_current();
	struct file* result_fd;
	if(file==NULL){
		return -1;
	}
	if(!pml4_get_page(thread_current()->pml4, file)){
		// printf("in ! pml get page\n");
		user_exit(-1);
	}
	result_fd = filesys_open(file);
	if(result_fd==NULL){
		return -1;
	}
	curr->fdt[curr->next_fd] = result_fd;
	curr->next_fd+=1;
	return curr->next_fd-1;
}

//int user_filesize (int fd){

//}