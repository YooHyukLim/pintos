#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <inttypes.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/page.h"

struct list frame_list; /* Frame Table */
struct lock frame_lock; /* Lock for frame table */

/* The frame element in frame table (list). */
struct frame_elem
{
  struct list_elem elem;
  struct thread *thread;
  struct spte *spte;

  void *frame;
};

void frame_init (void);
void * frame_alloc (struct spte *, enum palloc_flags);
void frame_dealloc (struct spte *);

#endif /* vm/frame.h */
