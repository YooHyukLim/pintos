#include "threads/malloc.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

static struct frame_elem * frame_get_from_code (struct spte *);
static void * frame_evict (enum palloc_flags);

/* Initialize the elements for frame. */
void
frame_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_list);
  list_init (&code_list);
}

/* Allocate a frame for upage and add it to frame table(list). */
void *
frame_alloc (struct spte *spte, enum palloc_flags flag)
{
  lock_acquire (&frame_lock);
  struct frame_elem *fe = NULL;
  
  /* If the spte's upage is code page, then first try to
     get the frame from code table. If there isn't the
     proper frame, then allocate a new frame. */
  if (!spte->writable
      && (fe = frame_get_from_code (spte))) {
    spte->fe = fe;
    list_push_back (&fe->sptes, &spte->celem);
    fe->ref++;

    /* Move the frame entry to the position which means
       recently accessed. */
    list_remove (&fe->elem);
    list_push_back (&frame_list, &fe->elem);
    lock_release (&frame_lock);

    return fe->frame;
  }

  void *frame = palloc_get_page (flag|PAL_USER);

  while (!frame) {
    /* If there is no frame to get, then evict a frame
       from the frame table. */
    frame = frame_evict (flag|PAL_USER);
  }

  /* Create the frame element and add to the list. */
  fe = malloc (sizeof (struct frame_elem));

  if (!fe) {
    palloc_free_page (frame);
    lock_release (&frame_lock);
    return NULL;
  }

  fe->frame = frame;
  fe->ofs = spte->ofs;
  list_init (&fe->sptes);
  list_push_back (&fe->sptes, &spte->celem);
  fe->ref = 1;
  spte->fe = fe;

  list_push_back (&frame_list, &fe->elem);

  struct code_entry *ce;
  if (!spte->writable
      && (ce = frame_get_code_entry (file_get_inode (spte->file)))
          != NULL) {
    list_push_back (&ce->frames, &fe->celem);
  }
  lock_release (&frame_lock);

  return frame;
}

/* Deallocate(free) the frame, if and only if there
   is no spte referencing the frame. If there is,
   just count down the ref count and remove the spte
   from the spte list of the frame. */
void
frame_dealloc (struct spte *spte)
{
  struct frame_elem *fe = spte->fe;

  if (!fe)
    return;

  lock_acquire (&frame_lock);

  list_remove (&spte->celem);
  spte->fe = NULL;

  if (fe->ref == 1) {
    list_remove (&fe->elem);

    if (!spte->writable)
      list_remove (&fe->celem);

    palloc_free_page (fe->frame);
    free (fe);
  } else {
    fe->ref--;
  }

  lock_release (&frame_lock);
}

/* Return the code entry which has the proper file(inode)
   from the code table. */
struct code_entry *
frame_get_code_entry (struct inode *inode)
{
  struct code_entry *ce = NULL;
  struct list_elem *elem;
  struct list_elem *end = list_end (&code_list);

  lock_acquire (&filesys_lock);
  for (elem = list_begin (&code_list);
       elem != end; elem = elem->next) {
    ce = list_entry (elem, struct code_entry, elem);

    if (ce->inode == inode) {
      lock_release (&filesys_lock);
      return ce;
    }
  }
  lock_release (&filesys_lock);

  return NULL;
}

/* Free the code entry, when there is no spte referencing
   the code entry. If there is, just count down the ref. */
void
frame_free_code_entry (struct inode *inode)
{
  struct code_entry *ce = frame_get_code_entry (inode);

  if (!ce)
    return;

  if (ce->ref == 1) {
    list_remove (&ce->elem);
    free (ce);
  } else
    ce->ref--;
}

/* Get the proper frame element of the file from the frame table,
   if the frame exists. */
static struct frame_elem *
frame_get_from_code (struct spte *spte)
{
  if (!spte || !spte->file)
    return NULL;

  struct code_entry *ce = frame_get_code_entry (file_get_inode (spte->file));
  struct frame_elem *fe = NULL;
  struct list_elem *elem = NULL, *end = NULL;

  if (!ce || list_empty (&ce->frames))
    return NULL;

  end = list_end (&ce->frames);
  for (elem = list_begin (&ce->frames);
       elem != end; elem = elem->next) {
    fe = list_entry (elem, struct frame_elem, celem);

    if (fe->ofs == spte->ofs)
      return fe;
  }

  return NULL;
}

/* Evict a frame, and return a new frame available. */
static void *
frame_evict (enum palloc_flags flag)
{
  struct list_elem *elem = NULL, *e = NULL;
  struct list_elem *end = list_end (&frame_list);
  struct frame_elem *fe = NULL;

  /* Found a frame which will be evicted by LRU (Second Chance). */
  for (elem = list_begin (&frame_list);
       elem != end; elem = list_next (elem)) {
    bool accessed = false;
    
    fe = list_entry (elem, struct frame_elem, elem);

    /* Check all threads referencing the frame. */
    for (e = list_begin (&fe->sptes);
         e != list_end (&fe->sptes); e = list_next (e)) {
      struct spte *spte = list_entry (e, struct spte, celem);

      /* If the frame is accessed recently, reset the frame. */
      if (pagedir_is_accessed (spte->t->pagedir, spte->upage)) {
        pagedir_set_accessed (spte->t->pagedir, spte->upage, false);
        accessed = true;
      }
    }

    if (!accessed)
      break;
  }

  if (elem == end)
    fe = list_entry (list_begin (&frame_list), struct frame_elem, elem);

  /* Evict the frame chosen. */
  struct spte *spte = list_entry (list_begin (&fe->sptes),
                                  struct spte, celem);

  /* If the frame is allowed to be written, then check its dirty and
     update, if and only if the frame is dirty. */
  if (spte->writable
      && pagedir_is_dirty (spte->t->pagedir, spte->upage)) {

    if (spte->mmap) {
      lock_acquire (&filesys_lock);
      file_write_at (spte->file, spte->upage, spte->read_bytes,
                     spte->ofs);
      lock_release (&filesys_lock);
    } else if (!swap_out (spte, fe->frame))
      return NULL;

  }

  /* Clear the upage in all pagedirs of threads referencing
     the frame. */
  for (e = list_begin (&fe->sptes);
       e != list_end (&fe->sptes); e = list_next (e)) {
    spte = list_entry (e, struct spte, celem);
    pagedir_clear_page (spte->t->pagedir, spte->upage);
    list_remove (&spte->celem);
    spte->fe = NULL;
  }

  if (!spte->writable)
    list_remove (&fe->celem);

  list_remove (&fe->elem);
  palloc_free_page (fe->frame);
  free (fe);

  /* Allocate a new frame. */
  return palloc_get_page (flag);
}
