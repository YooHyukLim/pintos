#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

#define MAX_ARGV 3
#define STD_IN 0
#define STD_OUT 1

#define USER_BOTTOM (void *)0x08048000

static void syscall_handler (struct intr_frame *);

static void is_user_addr (const void *);
static void is_readable_addr (const char *, unsigned);
static void is_writable_addr (uint8_t *, unsigned );
static void get_argv (int, int **, void *);

static int sys_wait (int);
static int sys_exec (const char *);
static bool sys_create (const char *, unsigned);
static bool sys_remove (const char *);
static int sys_open (const char *);
static int get_proper_fd (struct list *);
static int sys_filesize (int);
static int sys_read (int, void *, unsigned);
static int sys_write (int, const void *, unsigned);
static void sys_seek (int, unsigned);
static unsigned sys_tell (int);
static void sys_close (int);

void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *argv[MAX_ARGV];

  /* Check whether the esp is right address. */
  is_user_addr (f->esp);

  switch (*((int *) f->esp)) {
    /* Projects 2 and later. */
    case SYS_HALT:                   /* Halt the operating system. */
      shutdown_power_off ();
      break;

    case SYS_EXIT:                   /* Terminate this process. */
      get_argv (1, argv, f->esp);
      sys_exit (*argv[0]);
      break;

    case SYS_EXEC:                   /* Start another process. */
      get_argv (1, argv, f->esp);
      f->eax = sys_exec ((const char *) *argv[0]);
      break;

    case SYS_WAIT:                   /* Wait for a child process to die. */
      get_argv (1, argv, f->esp);
      f->eax = sys_wait (*argv[0]);
      break;

    case SYS_CREATE:                 /* Create a file. */
      get_argv (2, argv, f->esp);
      f->eax = sys_create ((const char *) *argv[0], (unsigned) *argv[1]);
      break;

    case SYS_REMOVE:                 /* Delete a file. */
      get_argv (1, argv, f->esp);
      f->eax = sys_remove ((const char *) *argv[0]);
      break;

    case SYS_OPEN:                   /* Open a file. */
      get_argv (1, argv, f->esp);
      f->eax = sys_open ((const char *) *argv[0]);
      break;

    case SYS_FILESIZE:               /* Obtain a file's size. */
      get_argv (1, argv, f->esp);
      f->eax = sys_filesize (*argv[0]);
      break;

    case SYS_READ:                   /* Read from a file. */
      get_argv (3, argv, f->esp);
      f->eax = sys_read (*argv[0], (void *) *argv[1], *argv[2]);
      break;

    case SYS_WRITE:                  /* Write to a file. */
      get_argv (3, argv, f->esp);
      f->eax = sys_write (*argv[0], (const void *) *argv[1], *argv[2]);
      break;

    case SYS_SEEK:                   /* Change position in a file. */
      get_argv (2, argv, f->esp);
      sys_seek (*argv[0], *argv[1]);
      break;

    case SYS_TELL:                   /* Report current position in a file. */
      get_argv (1, argv, f->esp);
      f->eax = sys_tell (*argv[0]);
      break;

    case SYS_CLOSE:                  /* Close a file. */
      get_argv (1, argv, f->esp);
      sys_close (*argv[0]);
      break;
    
    /* Project 3 and optionally project 4. */
    case SYS_MMAP:                   /* Map a file into memory. */
    case SYS_MUNMAP:                 /* Remove a memory mapping. */
      break;
    
    /* Project 4 only. */
    case SYS_CHDIR:                  /* Change the current directory. */
    case SYS_MKDIR:                  /* Create a directory. */
    case SYS_READDIR:                /* Reads a directory entry. */
    case SYS_ISDIR:                  /* Tests if a fd represents a directory. */
    case SYS_INUMBER:                /* Returns the inode number for a fd. */
      break;

    default:                         /* Returns Error when system call num is wrong. */ 
      f->eax = -1;
      break;
  }
}

/* Check whether the address is valid. The address shouldn't be kernel vaddr
   and should be bigger than USER_BOTTM. */
static void
is_user_addr (const void *addr)
{
  if (is_kernel_vaddr (addr)
      || get_user ((const uint8_t *) addr) == -1)
    sys_exit (-1);
}

/* Chech whether the given buffers are valid to be read. */
static void
is_readable_addr (const char *addr, unsigned bytes)
{
  unsigned i;
  
  for (i=0; i<bytes; i++)
    is_user_addr ((const void *) addr + i);
}

/* Check whether the given buffers are valid to be written. */
static void
is_writable_addr (uint8_t *udst, unsigned bytes)
{
  unsigned i;

  for (i=0; i<bytes; i++)
    if (is_kernel_vaddr (udst + i) || !put_user (udst + i, 0))
      sys_exit (-1);
}

/* Get the arguments from esp. */
static void
get_argv (int argc, int **argv, void *addr)
{
  int i = 0;

  do {
    addr += sizeof(int);
    argv[i++] = (int *) addr;

    /* Check the value of address is valid. */
    is_user_addr (addr);

  } while (i < argc);
}

/* Exit the process. */
void
sys_exit (int status)
{
  struct thread *cur = thread_current ();

  if (cur->process != NULL)
    cur->process->exit_status = status;
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

/* Wait a process which has the proper pid. */
static int
sys_wait (int pid)
{
  return process_wait (pid);
}

/* Execute new process. */
static int
sys_exec (const char *file_name)
{
  is_user_addr ((const void *) file_name);

  int pid = process_execute (file_name);
  struct process *p = get_child_process (pid);

  /* Check whether process made is NULL. */
  if (!p)
    return -1;

  /* Wait until load finishing. And if load was failed,
     return -1. */
  sema_down (&p->load_sema);
  if (!p->loaded || p->exit_status == -1)
    return -1;

  return pid;
}

/* Create a file. */
static bool
sys_create (const char *file, unsigned initial_size)
{
  is_user_addr ((const void *) file);

  lock_acquire (&filesys_lock);
  bool result = filesys_create (file, initial_size);
  lock_release (&filesys_lock);

  return result;
}

/* Remove the file. */
static bool
sys_remove (const char *file)
{
  is_user_addr ((const void *) file);

  lock_acquire (&filesys_lock);
  bool result = filesys_remove (file);
  lock_release (&filesys_lock);

  return result;
}

/* Open the file. */
static int
sys_open (const char *file)
{
  is_user_addr ((const void *) file);

  lock_acquire (&filesys_lock);
  int fd = -1;
  struct file *f = filesys_open (file);
  struct process *p = thread_current ()->process;
  struct process_file *pf = NULL;

  /* If f is NULL(filesys_open failed), then return -1. */
  if (f != NULL && p != NULL) {
    pf = malloc (sizeof(struct process_file));

    if (!pf) {
      lock_release (&filesys_lock);
      return fd;
    }
    
    fd = get_proper_fd (&p->file_list);

    /* Initiate the process_file's resources. And insert the
       process_file to the process's file_list in ascending order
       of fd. */
    pf->fd = fd;
    pf->file = f;
    list_push_back (&p->file_list, &pf->elem);
  }

  lock_release (&filesys_lock);

  return fd;
}

/* Get a proper fd by checking the file_list in struct process. */
static int
get_proper_fd (struct list *file_list)
{
  /* The file_list will be sorted in ascending order of fd. So
     the biggest fd can be found by getting the last element of
     the file_list. */
  struct list_elem *elem = list_rbegin (file_list);
  struct process_file *pf = NULL;

  /* If there is no element in list, return 3. Because, 0, 1, 2 are
     used for stdin, stdout, stderr. */
  if (elem == list_head (file_list))
      return 2;

  /* If there is an element in list, return its fd by adding 1. */
  pf = list_entry (elem, struct process_file, elem);
  return pf->fd + 1;
}

/* Get the filesize. */
static int
sys_filesize (int fd)
{
  int size = -1;
  lock_acquire (&filesys_lock);
  struct file *file = get_file_by_fd (fd);

  if (file != NULL)
    size = file_length (file);
  lock_release (&filesys_lock);

  return size;
}

/* Read the file which has the fd. If fd is STDIN, read by input_getc (). */
static int
sys_read (int fd, void *buffer, unsigned size)
{
  if (size == 0)
    return 0;

  is_writable_addr ((uint8_t *) buffer, size);

  unsigned read = -1;

  if (fd == STD_IN) {
    uint8_t *buf = (uint8_t *) buffer;

    for (read=0; read<size; read++) {
      buf[read] = input_getc ();
    }

    return (int) read;
  } else if (fd == STD_OUT) {
    return -1;
  }
  
  lock_acquire (&filesys_lock);
  struct file *file = get_file_by_fd (fd);

  if (file != NULL) {
    read = file_read (file, buffer, size);
  }
  lock_release (&filesys_lock);

  return (int) read;
}

/* If fd is STD_OUT, do print to console from buffer. If not, open
   proper file and write to it. */
static int
sys_write (int fd, const void *buffer, unsigned size)
{
  if (size == 0)
    return 0;

  is_readable_addr ((const char *) buffer, size);

  if (fd == STD_OUT) {
    putbuf (buffer, size);
    return size;
  } else if (fd == STD_IN) {
    return -1;
  }

  lock_acquire (&filesys_lock);
  unsigned written = -1;
  struct file *file = get_file_by_fd (fd);

  if (file != NULL) {
    written = file_write (file, buffer, size);
  }
  lock_release (&filesys_lock);

  return (int) written;
}

/* Set the position of fd. */
static void
sys_seek (int fd, unsigned position)
{
  lock_acquire (&filesys_lock);
  struct file *file = get_file_by_fd (fd);
  
  if (file != NULL) {
   file_seek (file, position);
  }
  lock_release (&filesys_lock);
}

/* Return the position of the file which has the fd. */
static unsigned
sys_tell (int fd)
{
  lock_acquire (&filesys_lock);
  struct file *file = get_file_by_fd (fd);
  unsigned pos = -1;

  if (file != NULL) {
    pos = file_tell (file);
  }
  lock_release (&filesys_lock);
  
  return pos;
}

/* Close the file which has the fd. */
static void
sys_close (int fd)
{
  if (fd < 0)
    return;
  close_by_fd (fd);
}

/* Get the struct file which has the right fd from the file_list of
   current thread. */
struct file *
get_file_by_fd (int fd)
{
  struct list *file_list = &thread_current ()->process->file_list;
  struct list_elem *elem = NULL;
  struct list_elem *end = list_end (file_list);
  struct process_file *pf = NULL;

  for (elem = list_begin (file_list); elem != end; elem = elem->next) {
    pf = list_entry (elem, struct process_file, elem);

    if (pf->fd == fd)
      return pf->file;
  }

  return NULL;
}

/* Close the file in file_list of current thread which has the right fd.
   If fd is CLOSE_ALL, then close all files in file_list. */
void
close_by_fd (int fd)
{
  lock_acquire (&filesys_lock);
  struct list *file_list = &thread_current ()->process->file_list;
  struct list_elem *elem = NULL, *tmp = NULL;
  struct list_elem *end = list_end (file_list);
  struct process_file *pf = NULL;

  for (elem = list_begin (file_list); elem != end; elem = elem->next) {
    pf = list_entry (elem, struct process_file, elem);

    if (pf->fd == fd || fd == CLOSE_ALL) {
      tmp = elem;
      elem = elem->prev;

      list_remove (tmp);
      file_close (pf->file);
      free (pf);

      if (pf->fd == fd) {
        lock_release (&filesys_lock);
        return;
      }
    }
  }

  lock_release (&filesys_lock);
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}
