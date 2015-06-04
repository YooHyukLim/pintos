#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <inttypes.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/page.h"
#include "filesys/file.h"

struct spte;

struct list code_list;  /* Code Table. */
struct list frame_list; /* Frame Table. */
struct lock frame_lock; /* Lock for frame table. */

/* The frame element in frame table (list). */
struct frame_elem
{
  struct list_elem elem;       /* List element for frame table. */
  struct list_elem celem;      /* List element for code entry of 
                                  code table. */
  struct list sptes;           /* List for sptes referencing this. */
  //struct spte *spte;

  struct code_entry *ct_entry; /* Code entry of this frame. */
  int ref;                     /* Reference count. */

  void *frame;                 /* Phys address of this frame. */
  off_t ofs;                   /* Offset of the file. This will be
                                  the key from code entry. */
};

/* The code entry for code table. */
struct code_entry
{
  int ref;                     /* Reference count. */
  struct list_elem elem;       /* List element for code table. */
  struct list frames;          /* List of frames. */
  struct inode *inode;         /* The file information of code entry. */
};

void frame_init (void);
void * frame_alloc (struct spte *, enum palloc_flags);
void frame_dealloc (struct spte *);
struct code_entry * frame_get_code_entry (struct inode *);
void frame_free_code_entry (struct inode *);

#endif /* vm/frame.h */
