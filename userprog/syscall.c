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
struct thread* find_child(tid_t pid);
unsigned user_tell(int fd);

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
		case SYS_FILESIZE:
			f->R.rax = user_filesize(f->R.rdi);
			break;
		case SYS_CLOSE:
			user_close(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = user_read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = user_write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			user_seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = user_tell(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = user_wait(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = user_exec (f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = user_fork(f->R.rdi, f);
			break;
	}
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
	result_fd= filesys_open(file);
	if(result_fd==NULL){
		return -1; 
	}
	curr->fdt[curr->next_fd] = result_fd;
	curr->next_fd+=1;
	return curr->next_fd-1;
}

int user_filesize (int fd){
	return file_length (thread_current()->fdt[fd]);
}

void user_close (int fd){
	if(fd>=64||fd<0){
		return;
	}
	struct thread *curr = thread_current();
	file_close(curr->fdt[fd]);
	curr->fdt[fd] = NULL;
}

int user_read (int fd, void *buffer, unsigned size){
	struct thread *curr = thread_current();
	int read_bytes;
	if(fd<0||fd>=64||curr->fdt[fd]==NULL){
		user_exit(-1);
	}
	if(!pml4_get_page(thread_current()->pml4, buffer)){
		user_exit(-1);
		
	}
	
	read_bytes = file_read(curr->fdt[fd], buffer, (off_t)size);
	return read_bytes;
}

int user_write(int fd, const void *buffer, unsigned size){
	struct thread *curr = thread_current();
	int written_bytes;
	if(fd<0||fd>=64){
		user_exit(-1);
	}
	if(!pml4_get_page(thread_current()->pml4, buffer)){
		user_exit(-1);
	}
	if(fd==1){
		putbuf(buffer, size);
		written_bytes = size;
	}
	if(curr->fdt[fd]==NULL){
		return 0;
	}
	written_bytes = file_write(curr->fdt[fd], buffer, size);
	return written_bytes;
}

tid_t user_fork(const char *thread_name, struct intr_frame *f){
	/* 부모 prc : 자식 프로세스의 pid를 반환
		자식 프로스가 리소스 복제 실패 -> TID_ERROR 반환
		자식 프로세스가 성공적으로 복제되었는지 여부를 알 때까지 fork 반환 X
	*/
	// 자식 prc : 반환 값이 0, 복제된 리소스(파일 식별자, 가상 메모리 공간) 있어야 함.
	tid_t pid = process_fork(thread_name);
	if(pid==TID_ERROR){
		return TID_ERROR;
	}
	struct thread *child = find_child(pid);
	sema_down(&child->fork_sema);
	if(child->exit_status==-1){
		return TID_ERROR;
	}
	if(child->status == -1){
		return TID_ERROR;
	}
	return pid;
}

int user_wait(tid_t pid){
	//sema_down(&thread_current()->wait_sema);
	return process_wait(pid);
}

int user_exec (const char *cmd_line){
	int result;
	//sema_down(&thread_current()->exec_sema);
	result = process_exec(cmd_line);
	if(result == -1){
		user_exit(-1);
	}
	else{
		return NULL;
	}

}

void user_seek(int fd, unsigned position){
	file_seek(thread_current()->fdt[fd], position);
}

unsigned user_tell(int fd){
	return file_tell(thread_current()->fdt[fd]);
}
/*
struct thread* find_child(tid_t pid){
	struct list_elem *cur_elem;
	struct thread* cur_thread;
	cur_elem=list_begin(&thread_current()->t_child_list);
	while(cur_elem!=list_end(&thread_current()->t_child_list)){
		printf("a : %d\n", pid);
		cur_thread = list_entry(cur_elem, struct thread, t_child_elem);
		printf("cur_thread : %d %d\n", cur_thread->tid, pid);
		if(cur_thread->tid == pid){
			printf("c : %d\n", pid);
			return cur_thread;
		}
		printf("d : %d\n", pid);
		cur_elem = list_next(cur_elem);
		printf("e : %d\n", pid);
	}
	return TID_ERROR;
}*/