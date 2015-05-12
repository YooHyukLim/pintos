#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/synch.h"
#include "filesys/file.h"

#define CLOSE_ALL -1

/* A Struct for child processes. */
struct process {
  int pid;                          /* Pid of process. */
  int exit_status;                  /* The status code of exiting. */
  struct semaphore sema;            /* The semaphore for process_wait. */
  bool loaded;                      /* The result of load. */
  struct semaphore load_sema;       /* Semaphore for checking whether
                                       the process was loaded. */
  struct list file_list;            /* The list for files opened and
                                       sharing with the processes which
                                       have same pid. */
  struct list_elem elem;            /* The list element for the process. */
};

/* A Struct for file information of process. */
struct process_file {
  int fd;                   /* A file descriptor of the file. */
  struct file *file;        /* The information of the file. */
  struct list_elem elem;    /* The list element. */
};

struct lock filesys_lock;

void syscall_init (void);
void sys_exit (int);
struct file *get_file_by_fd (int);
void close_by_fd (int);
struct process * create_process (int);
struct process * add_child_process (int);
struct process * get_child_process (int);
void remove_child_process (struct process *);
int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);

#endif /* userprog/syscall.h */
