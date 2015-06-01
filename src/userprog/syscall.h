#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/synch.h"
#include "filesys/file.h"
#include "vm/page.h"

#define CLOSE_ALL -1

struct lock filesys_lock;

void syscall_init (void);
void sys_exit (int);
struct file *get_file_by_fd (int);
void close_by_fd (int);
void munmap_by_id (mapid_t);
int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);

#endif /* userprog/syscall.h */
