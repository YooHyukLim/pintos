#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"

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

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct process * create_process (int);
struct process * add_child_process (int);
struct process * get_child_process (int);
void remove_child_process (struct process *);
void free_child_process (void);

#endif /* userprog/process.h */
