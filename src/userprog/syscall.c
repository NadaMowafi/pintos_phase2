#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "process.h"


static void syscall_handler (struct intr_frame *);
static void check_args(int argc);
static void check_pointer(void * ptr);
static struct file * get_file(int fd);
static int add_file(struct file *f);
static void remove_file(int fd);

static struct lock lock;
uint32_t* stack_pointer = NULL;


void
syscall_init (void) 
{
    
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&lock);
   
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
    
  stack_pointer = f->esp;
  check_pointer(stack_pointer);
  int syscall_number = (int) *stack_pointer;

   switch(syscall_number)
  { 
    case SYS_HALT: 
    {
       shutdown_power_off();
       break;
    }

    case SYS_EXIT:
    {
        check_args(1);
        int status = (int)(*(stack_pointer + 1));
        exit(status);
        break;
    }

    case SYS_EXEC:
    {
       check_args(1);
       char*name=*(char**)(stack_pointer+1);
       check_pointer(name);
       f->eax= exec_func(name);
       break;
      }

    case SYS_WAIT:
     { 
         
        check_args(1);
        pid_t pid = (pid_t)(*(stack_pointer + 1));
        f->eax = process_wait(pid);
        break;
        

    }

    case SYS_CREATE:                 /* Create a file. */
    {
      check_args(2);
      f->eax = create((const char *)*(stack_pointer + 1), (unsigned )(*(stack_pointer + 2)));
      break;
    } 

    case SYS_REMOVE:                 /* Delete a file. */
    {
      check_args(1);
      f->eax = remove((const char *)*(stack_pointer + 1));
      break;
    }

    case SYS_OPEN:                   /* Open a file. */
    {
      check_args(1);
      f->eax = open((const char *)*(stack_pointer + 1));
      break;
    }
       case SYS_FILESIZE:               /* Obtain a file's size. */
    { 
      check_args(1);
      f->eax = filesize((int)(*(stack_pointer + 1)));
      break;
    }
    case SYS_READ:                   /* Read from a file. */
    {
      check_args(3);
      f->eax = read((int)(*(stack_pointer + 1)), (void *) *(stack_pointer + 2), (unsigned)(*(stack_pointer + 3)));
      break;
    }
    case SYS_WRITE:                  /* Write to a file. */
    {
      check_args(3);
      f->eax = write((int)(*(stack_pointer + 1)), (const char *)*(stack_pointer + 2), (unsigned)(*(stack_pointer + 3)));
      break;
    }
    case SYS_SEEK:                   /* Change position in a file. */
    {
      check_args(2);
      seek((int)*(stack_pointer + 1), (unsigned)(*(stack_pointer + 2)));
      break;
    }
    case SYS_TELL:                   /* Report current position in a file. */
    {
      check_args(1);
      f->eax = tell((int)(*(stack_pointer + 1)));
      break;
    }
    case SYS_CLOSE:                  /* Close a file. */
    {
      check_args(1);
      close((int)(*(stack_pointer + 1)));
      break;
    }
  }
}


void exit(int status)
{

     struct list_elem *e;
      //loop on the list of children 
      for (e = list_begin (&thread_current()->parent->list_of_children); e != list_end (&thread_current()->parent->list_of_children);
           e = list_next (e))
        {
          //for each loop change the element to struct child
          struct child *f = list_entry (e, struct child, elem);
          if(f->tid == thread_current()->tid)   
          {
          	f->used = true;
          	f->exit_error = status;
          }
        }


	thread_current()->exit_error = status;

	if(thread_current()->parent->waitingOnChild == thread_current()->tid)
		sema_up(&thread_current()->parent->wait_on_child_sema);   //wakeup the parent

	thread_exit();


 /* struct thread* parent = thread_current()->parent;
  printf("%s: exit(%d)\n", thread_current()->name, status);
  if(parent !=NULL)     //if the current thread has a parent  (parent != NULL)
  {
      parent->child_status = status;
  } 
  //the child status of the parent thread (if it exists) is updated with the exit status of the current thread. 
  //This facilitates communication between parent and child threads 
  thread_exit();     //it calls process_exit function*/
}


bool create (const char *file, unsigned initial_size){ 
  if(file == NULL) exit(-1);
  void * v = (void *)file;
  check_pointer(v);
  lock_acquire(&lock);
  bool ret = filesys_create (file, initial_size);
  lock_release(&lock);
  return ret;
}

/* Deletes the file called file. Returns true if successful, false otherwise.*/
bool remove (const char *file){
  if(file == NULL) exit(-1);
  void * v = (void *)file;
  check_pointer(v);
  lock_acquire(&lock);
  bool ret = filesys_remove (file); 
  lock_release(&lock);
  return ret;
}

/* Opens the file called file. Returns a nonnegative integer handle called 
   a "file descriptor" (fd), or -1 if the file could not be opened.*/
int open (const char *file){
  if(file == NULL) exit(-1);
  void * v = (void *)file;
  check_pointer(v);
  int fd = -1;
  lock_acquire(&lock);
  struct file *f = filesys_open(file);
  lock_release(&lock);
  if(f == NULL){
     return fd =-1;
  }
  fd = add_file(f);
  return fd;
}

/* Returns the size, in bytes, of the file open as fd.*/
int filesize (int fd){
  struct file *file = get_file(fd);
  if(file == NULL) exit(-1);
  lock_acquire(&lock);
  int ret = (int)file_length(file); 
  lock_release(&lock);
  return ret;  
}

/*Reads size bytes from the file open as fd into buffer. 
Returns the number of bytes actually read */
int read (int fd, void *buffer, unsigned size)
{
  if(buffer == NULL) exit(-1);
  void * v = (void *)buffer;
  check_pointer(v);
  lock_acquire(&lock); 
  if(fd == 0)
  {
    char *line = (char*) buffer;
    unsigned  i;
    for (i = 0; i < size; i++)
      line[i] = input_getc();
    return i;
  }
  struct file *f = get_file(fd);
  if(f == NULL) exit(-1);
  int ret = file_read(f, buffer, size);
  lock_release(&lock);
  return ret;
}


/*Writes size bytes from buffer to the open file fd. 
Returns the number of bytes actually written*/
int write (int fd, const void *buffer, unsigned size) 
{
  if(buffer == NULL) exit(-1);
  void * v = (void *)buffer;
  check_pointer(v);
  unsigned temp = size;
  lock_acquire(&lock);
  
  if (!is_user_vaddr(buffer+size)){
    lock_release (&lock);
    exit (-1);
  }
  
  if (fd == 1)    // write to the console
  { 
    while(size > 100){
      putbuf(buffer, 100);
      size = size - 100;
      buffer = buffer + 100;
    }
    putbuf(buffer, size);
  }else if(fd == 0){
    lock_release(&lock);  
    return 0;
  }else{
    struct file *file = get_file(fd);
    if(file == NULL){
      lock_release(&lock);  
      return 0;
    }
    temp = file_write(file, buffer, size);
  }
  lock_release(&lock);  
  return temp; 
}

/*Changes the next byte to be read or written in open file fd to position */
void seek (int fd, unsigned position)
{
  struct file *f = get_file(fd);
  if(f == NULL) exit(-1);
  lock_acquire(&lock);
  file_seek(f, position);
  lock_release(&lock);
}

/*Returns the position of the next byte to be read or written in open file fd */
unsigned tell (int fd)
{
   struct file *f = get_file(fd);
   if(f == NULL) exit(-1);
   lock_acquire(&lock);
   unsigned ret = file_tell(f);
   lock_release(&lock);
   return ret;
}

/*Closes file descriptor fd */
void close (int fd)
{
   struct file *f = get_file(fd);
   if(f == NULL) exit(-1);
   lock_acquire(&lock);
   file_close (f);
   remove_file(fd);
   lock_release(&lock);
}

/* check if valid user pointer */
static void check_pointer(void * ptr){
  if (!is_user_vaddr (ptr) || pagedir_get_page(thread_current()->pagedir,ptr) == NULL){
    if(lock_held_by_current_thread(&lock))
      lock_release (&lock);
    exit (-1);
  }
}

/* Check arguments of syscall, terminate if invalid*/
static void check_args(int argc){
  int i;
  for (i = 1; i <= argc; i++){
    check_pointer(stack_pointer + i);
    if(stack_pointer + i == NULL)
      exit(-1);
  }
}

/*get file from fd */
static struct file * get_file(int fd)
{
   struct list_elem *e;
   struct list *fd_table = &thread_current()->fd_table; 
   for(e = list_begin(fd_table); e != list_end(fd_table); e = list_next(e)){
     struct descriptor *f_descriptor = list_entry(e, struct descriptor, fd_elem);
     if(f_descriptor->fd == fd)
        return f_descriptor->file;
  }
  return NULL;  
}

static int add_file(struct file *f)
{
    struct thread *cur_th = thread_current();
    struct descriptor *d = malloc(sizeof(struct descriptor));
    d->file = f;
    d->fd = cur_th->fileNumber++;
    list_push_back(&cur_th->fd_table, &d->fd_elem);
    return d->fd;      
}

static void remove_file(int fd)
{
   struct list_elem *e;
   struct list *fd_table = &thread_current()->fd_table; 
   for(e = list_begin(fd_table); e != list_end(fd_table); e = list_next(e)){
     struct descriptor *f_descriptor = list_entry(e, struct descriptor, fd_elem);
     if(f_descriptor->fd == fd){
             list_remove(e);
             free(f_descriptor);
             return;
     }
   }       
}
int exec_func(char *file_name)
{
	lock_acquire(&lock);
	char * name = malloc (strlen(file_name)+1);
	  strlcpy(name, file_name, strlen(file_name)+1);
	  
	  char * save_ptr;
	  name = strtok_r(name," ",&save_ptr);

	 struct file* f = filesys_open (name);

	  if(f==NULL)
	  {
	  	lock_release(&lock);
	  	return -1;
	  }
	  else
	  {
         
	  	file_close(f);
	  	lock_release(&lock);
	  	return process_execute(file_name);
	  }
}

 

