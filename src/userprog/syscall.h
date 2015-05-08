#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/synch.h"
#include "filesys/file.h"

#define CLOSE_ALL -1

/* A Struct for child processes. */
struct process {
  int pid;                  /* Pid of process. */
  int exit_status;          /* The status code of exiting. */
  struct semaphore sema;    /* The semaphore for process_wait. */
  struct list file_list;        /* The list for files opened and sharing with
                               the processes which have same pid. */
  struct list_elem elem;    /* The list element for the process. */
};

/* A Struct for file information of process. */
struct process_file {
  int fd;                   /* A file descriptor of the file. */
  struct file *file;        /* The information of the file. */
  struct list_elem elem;    /* The list element. */
};

void syscall_init (void);
void close_by_fd (int);
struct process * add_child_process (int);
struct process * get_child_process (int);
void remove_child_process (struct process *);

#endif /* userprog/syscall.h */
