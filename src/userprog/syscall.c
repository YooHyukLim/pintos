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
#define STD_ERR 2

#define USER_BOTTOM (void *)0x08048000

static void syscall_handler (struct intr_frame *);

static bool is_user_addr (const void *);
//static int convert_phys_page (const void *);
static void get_argv (int, int **, void *);

static int sys_wait (int);
static int sys_exec (const char *);
static bool sys_create (const char *, unsigned);
static bool sys_remove (const char *);
static int sys_open (const char *);
//static bool cmp_fd (const struct list_elem *, const struct list_elem *,
//          void *aux UNUSED);
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

  //thread_exit ();
  //hex_dump ((int) f->esp, f->esp, 0xC0000000-(int)f->esp,true);
 
  /* Check whether the esp is right address. */
  if (!is_user_addr (f->esp))
    sys_exit (-1);

  //printf("esp: %#010x\n", (unsigned)f->esp);
  //hex_dump ((int) f->esp, f->esp, sizeof(int),true);

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
      //*argv[0] = convert_phys_page ((const void *) *argv[0]);
      f->eax = sys_exec ((const char *) *argv[0]);
      break;

    case SYS_WAIT:                   /* Wait for a child process to die. */
      get_argv (1, argv, f->esp);
      f->eax = sys_wait (*argv[0]);
      break;

    case SYS_CREATE:                 /* Create a file. */
      get_argv (2, argv, f->esp);
      //*argv[0] = convert_phys_page ((const void *) *argv[0]);
      f->eax = sys_create ((const char *) *argv[0], (unsigned) *argv[1]);
      break;

    case SYS_REMOVE:                 /* Delete a file. */
      get_argv (1, argv, f->esp);
      //*argv[0] = convert_phys_page ((const void *) *argv[0]);
      f->eax = sys_remove ((const char *) *argv[0]);
      break;

    case SYS_OPEN:                   /* Open a file. */
      get_argv (1, argv, f->esp);
      //*argv[0] = convert_phys_page ((const void *) *argv[0]);
      f->eax = sys_open ((const char *) *argv[0]);
      break;

    case SYS_FILESIZE:               /* Obtain a file's size. */
      get_argv (1, argv, f->esp);
      f->eax = sys_filesize (*argv[0]);
      break;

    case SYS_READ:                   /* Read from a file. */
      get_argv (3, argv, f->esp);
      //*argv[1] = convert_phys_page ((const void *) *argv[1]);
      f->eax = sys_read (*argv[0], (void *) *argv[1], *argv[2]);
      break;

    case SYS_WRITE:                  /* Write to a file. */
      get_argv (3, argv, f->esp);
      //*argv[1] = convert_phys_page ((const void *) *argv[1]);
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
static bool
is_user_addr (const void *addr)
{
  if (is_kernel_vaddr (addr) || addr < USER_BOTTOM
      || !pagedir_get_page (thread_current ()->pagedir, addr))
    return false;
  
  return true;
}

/* Convert the user virtual address to physical address. */
//static int
//convert_phys_page (const void *addr)
//{
//  if (!is_user_addr (addr))
//    sys_exit (-1);
//
//  /* Convert the address. */
//  void *result = pagedir_get_page (thread_current ()->pagedir, addr);
//
//  return (int) result;
//}


/* Get the arguments from esp. */
static void
get_argv (int argc, int **argv, void *addr)
{
  int i = 0;

  do {
    addr += sizeof(int);
    argv[i++] = (int *) addr;

    /* Check the value of address is valid. */
    if (!is_user_addr (addr))
      sys_exit (-1);

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
  if (!is_user_addr ((const void *) file_name))
    sys_exit (-1);

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
  if (!is_user_addr ((const void *) file))
    sys_exit (-1);

  lock_acquire (&filesys_lock);
  bool result = filesys_create (file, initial_size);
  lock_release (&filesys_lock);

  return result;
}

/* Remove the file. */
static bool
sys_remove (const char *file)
{
  if (!is_user_addr ((const void *) file))
    sys_exit (-1);

  lock_acquire (&filesys_lock);
  bool result = filesys_remove (file);
  lock_release (&filesys_lock);

  return result;
}

/* Open the file. */
static int
sys_open (const char *file)
{
  if (!is_user_addr ((const void *) file))
    sys_exit (-1);

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
//    list_insert_ordered (&p->file_list, &pf->elem,
//                          (list_less_func *) cmp_fd, NULL);
  }

  lock_release (&filesys_lock);

  return fd;
}

//static bool
//cmp_fd (const struct list_elem *a, const struct list_elem *b,
//          void *aux UNUSED)
//{
//  struct process_file *pf_a = list_entry (a, struct process_file, elem);
//  struct process_file *pf_b = list_entry (b, struct process_file, elem);
//
//  if (pf_a->fd < pf_b->fd)
//    return true;
//
//  return false;
//}

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
  if (!is_user_addr ((const void *) buffer)
      || !is_user_addr ((const void *) buffer + size))
    sys_exit (-1);

  unsigned read = -1;

  if (fd == STD_IN) {
    uint8_t *buf = (uint8_t *) buffer;

    for (read=0; read<size; read++) {
      buf[read] = input_getc ();
    }

    return (int) read;
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
  if (!is_user_addr ((const void *) buffer)
      || !is_user_addr ((const void *) buffer + size))
    sys_exit (-1);

  if (fd == STD_OUT) {
    putbuf (buffer, size);
    return size;
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

/* Create a new process. */
struct process *
create_process (int pid)
{
  struct process *p = malloc (sizeof (struct process));

  if (!p)
    return NULL;

  p->pid = pid;

  /* Initiate sema for 0, and process_wait will sema_down this, and
     process_exit will sema_up this. */
  sema_init (&p->sema, 0);
  
  /* Initiate the resources related with load. */
  p->loaded = false;
  sema_init (&p->load_sema, 0);
  
  /* Initiate the file_list which will take the information of files
     which are opened by same processes. */
  list_init (&p->file_list);

  return p;
}

/* Create  new struct of process, and Add to list of cur thread. */
struct process *
add_child_process (int pid)
{
  struct thread *cur_t = thread_current ();
  struct process *p = create_process (pid);

  if (!p)
    return NULL;

  /* Push the child process to the list of current thread. */
  list_push_front (&cur_t->child_list, &p->elem);

  return p;
}

/* Get the child process which has a proper pid. */
struct process *
get_child_process (int pid)
{
  struct thread *cur = thread_current ();
  struct process *p;
  struct list_elem *elem;
  struct list_elem *end = list_end (&cur->child_list);

  for (elem = list_begin (&cur->child_list);
       elem != end; elem = elem->next) {
    p = list_entry (elem, struct process, elem);
    
    if (p->pid == pid)
      return p;
  }

  return NULL;
}

/* Remove the child process from the list of cur thread.  */
void
remove_child_process (struct process *p)
{
  list_remove (&p->elem);
  free (p);
}
