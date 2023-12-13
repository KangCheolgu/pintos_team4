#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "filesys/file.h"



void syscall_entry (void);
void syscall_handler (struct intr_frame *);
int fd = 2;
typedef int pid_t;

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
bool syscall_remove (const char *file);

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
	// printf("%d\n",f->R.rax);
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
		f->R.rax = syscall_exec(f->R.rdi);
		break;
	case SYS_WAIT :
		f->R.rax = syscall_wait(f->R.rdi);
		break;
	case SYS_CREATE :
		f->R.rax = syscall_create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE :
		f->R.rax = syscall_remove (f->R.rdi);
		break;
	case SYS_OPEN :
		f->R.rax = syscall_open(f->R.rdi);
		break;
	case SYS_FILESIZE :
		f->R.rax = syscall_filesize(f->R.rdi);
		break;
	case SYS_READ :
		f->R.rax = syscall_read (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE :
		f->R.rax = syscall_write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_CLOSE :
		syscall_close(f->R.rdi);
		break;
	case SYS_SEEK :
		syscall_seek (f->R.rdi, f->R.rsi); 	
		break;
	}
}

void syscall_exit (int status)
{
    struct thread *curr = thread_current (); 
	curr->sys_stat = status; 
	printf("%s: exit(%d)\n" , curr -> name , curr->sys_stat);
	for(int i = 0; i < 64; i++){
		if (curr->file_descripter_table[i] != NULL){
			file_close(curr->file_descripter_table[i]);
			curr->file_descripter_table[i] = NULL;
		}
	}
	// file_close(curr->current_file);

    thread_exit();
}  

void syscall_halt (void)
{
    power_off();
}

// pml4_for_each (parent->pml4, duplicate_pte, parent)
// %RBX, %RSP, %RBP와 %R12 - %R15 만 복사하라?
pid_t syscall_fork (const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name , f);
}

int syscall_exec (const char *cmd_line) {
	// bad-pointer                  
	if(!pml4_get_page(thread_current()->pml4, cmd_line)){
		syscall_exit(-1);
	}
	char *f_copy;
	f_copy = palloc_get_page (0);

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	strlcpy (f_copy, cmd_line, PGSIZE);
	tid_t tid = process_exec (f_copy);

	if(tid == -1){
		syscall_exit(-1);
	}

	return tid;
}

bool syscall_create(char *file, unsigned initial_size){
	// bad-pointer                  
	if(!pml4_get_page(thread_current()->pml4, file)){
		syscall_exit(-1);
	}

	return filesys_create(file, initial_size);

}
// 파일 디스크립터 반환 파일 디스크립터가 겹치면 안됨
int syscall_open (const char *file) {
	// open-bad-ptr
	if(!pml4_get_page(thread_current()->pml4, file)){
		// printf("in ! pml get page\n");
		syscall_exit(-1);
	}

	// open-null
	if(file == NULL){
		// printf("in file == null\n");
		return -1;
	}

	// open-empty
	if(strlen(file) == 0){
		// printf("in file == empty\n");
		return -1;
	}

	struct file *_file = filesys_open (file);

	// open-missing
	if(_file == NULL){
		// printf("in file == null\n");
		return -1;
	}

	for(int i = 2; i < 64; i++){
		if(thread_current()->file_descripter_table [i] == NULL) {
			thread_current()->file_descripter_table [i] = _file;

			if (strcmp (thread_current()->name, file) == false)
          		file_deny_write (_file);

			return i;
		}
	}

	return -1;

}

void syscall_close(int fd){

	if(fd < 0 || fd > 63){
		syscall_exit(-1);
	}

	if (thread_current()->file_descripter_table[fd] != NULL){
		file_close(thread_current()->file_descripter_table[fd]);
	} 

	thread_current()->file_descripter_table[fd] = NULL;

}

int syscall_read (int fd, void *buffer, unsigned size) {
	if(buffer == NULL) return -1; 

	if(!pml4_get_page(thread_current()->pml4, buffer)) syscall_exit(-1);

	if(fd < 0 || fd > 63) syscall_exit(-1);

	if(fd == 1) syscall_exit(-1);

	struct file *_file = thread_current()->file_descripter_table[fd];

	int result = file_read (_file, buffer, size);

	return result;
}

int syscall_filesize (int fd) {
	return file_length(thread_current()->file_descripter_table[fd]);
}


int syscall_write (int fd, const void *buffer, unsigned size) {
	if(!pml4_get_page(thread_current()->pml4, buffer)) syscall_exit(-1);

	if(fd < 0 || fd > 63) syscall_exit(-1);

	if(buffer == NULL) syscall_exit(-1);

	if(fd == 0) syscall_exit(-1);

	if(fd == 1){
		putbuf(buffer,size);
		return size;
	} 

	if(thread_current()->file_descripter_table[fd] == NULL) return 0;

	struct file *_file = thread_current()->file_descripter_table[fd];

	if (_file->deny_write) file_deny_write (_file);

	return file_write (_file, buffer, size);
}

// pid 를 가진 프로세스가 종료 될 때까지 대기, 자식의 종료상태 가져옴 sys_stat
// pid 가 살아있다면 해당 프로세스가 종료될때까지 기다립니다.
// 종료 상태를 반환 sys stat
// zj
int
syscall_wait (pid_t pid) {
	// 만약 pid가 exit를 호출하지 않았지만 커널에 의해 종료되었다면, -1을 반환합니다.
	// 부모 프로세스는 종료된 자식 프로세스에 대해 wait를 호출할 수 있습니다.
	// 종료된 자식 프로세스의 종료 상태를 반환합니다.
	return process_wait(pid);
}

void syscall_seek (int fd, unsigned position) {
	struct file *tmp = thread_current()->file_descripter_table[fd];

	file_seek(tmp, position);
}

bool syscall_remove (const char *file) {

	if(!pml4_get_page(thread_current()->pml4, file)){
		syscall_exit(-1);
	}

	char *f_copy;
	f_copy = palloc_get_page (0);

	strlcpy (f_copy, file, PGSIZE);

	return filesys_remove(f_copy);
}