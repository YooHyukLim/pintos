#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <inttypes.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "vm/page.h"

struct list frame_list; /* Frame Table */
struct lock frame_lock; /* Lock for frame table */

/* The frame element in frame table (list). */
struct frame_elem
{
  struct list_elem elem;
  struct spte *spte;
  struct thread *thread;

  void *frame;
};

void * frame_alloc (struct spte *, enum palloc_flags);
void frame_dealloc (void *);

#endif /* vm/frame.h */
