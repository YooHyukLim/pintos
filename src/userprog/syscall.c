#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/sync.h"

#define MAX_ARGV 3
#define STD_IN 0
#define STD_OUT 1
#define STD_ERR 2

#define USER_BOTTOM (void *)0x08048000

static void syscall_handler (struct intr_frame *);

static bool is_user_addr (const void *addr);
static void get_argv (int argc, int **argv, void *addr);

static void sys_exit (int status);
static int sys_write (int fd, const void *buffer, unsigned size);

static struct lock filesys_lock;

void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int sys_num = *((int *) f->esp);
  int *argv[MAX_ARGV];

  //thread_exit ();
  //hex_dump ((int) f->esp, f->esp, 0xC0000000-(int)f->esp,true);

  if (!is_user_addr (f->esp))
    sys_exit (-1);

  switch (sys_num) {
    /* Projects 2 and later. */
    case SYS_HALT:                   /* Halt the operating system. */
    case SYS_EXIT:                   /* Terminate this process. */

      get_argv (1, argv, f->esp);
      sys_exit (*argv[0]);
      break;

    case SYS_EXEC:                   /* Start another process. */
    case SYS_WAIT:                   /* Wait for a child process to die. */
    case SYS_CREATE:                 /* Create a file. */
    case SYS_REMOVE:                 /* Delete a file. */
    case SYS_OPEN:                   /* Open a file. */
    case SYS_FILESIZE:               /* Obtain a file's size. */
    case SYS_READ:                   /* Read from a file. */
    case SYS_WRITE:                  /* Write to a file. */

      get_argv (3, argv, f->esp);
      f->eax = sys_write (*argv[0], (const char *) *argv[1], *argv[2]);
      break;

    case SYS_SEEK:                   /* Change position in a file. */
    case SYS_TELL:                   /* Report current position in a file. */
    case SYS_CLOSE:                  /* Close a file. */
    
    /* Project 3 and optionally project 4. */
    case SYS_MMAP:                   /* Map a file into memory. */
    case SYS_MUNMAP:                 /* Remove a memory mapping. */
    
    /* Project 4 only. */
    case SYS_CHDIR:                  /* Change the current directory. */
    case SYS_MKDIR:                  /* Create a directory. */
    case SYS_READDIR:                /* Reads a directory entry. */
    case SYS_ISDIR:                  /* Tests if a fd represents a directory. */
    case SYS_INUMBER:                 /* Returns the inode number for a fd. */
    default:
      break;
  }
}

/* Check whether the address is valid. The address shouldn't be kernel vaddr
   and should be bigger than USER_BOTTM. */
static bool
is_user_addr (const void *addr)
{
  if (is_kernel_vaddr (addr) || addr < USER_BOTTOM)
    return false;
  
  return true;
}

/* Get the arguments from esp. */
static void
get_argv (int argc, int **argv, void *addr)
{
  int i;

  addr += sizeof(int);
  for (i=0; i<argc; i++) {
    argv[i] = (int *) addr;
    addr += sizeof(int);
  }
}

/* Exit the process. */
static void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

/* If fd is STD_OUT, do print to console from buffer. If not, open proper file
   and write to it. */
static int
sys_write (int fd, const void *buffer, unsigned size)
{
   if (fd == STD_OUT) {
     putbuf (buffer, size);
     return size;
   }

   return size;
} 
