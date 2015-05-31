#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

static void * frame_evict (enum palloc_flags);

/* Initialize the elements for frame. */
void
frame_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_list);
}

/* Allocate a frame for upage and add it to frame table(list). */
void *
frame_alloc (struct spte *spte, enum palloc_flags flag)
{
  void *frame = palloc_get_page (flag|PAL_USER);

  lock_acquire (&frame_lock);
  while (!frame) {
    /* If there is no frame to get, then evict a frame
       from the frame table. */
    frame = frame_evict (flag|PAL_USER);
  }
  lock_release (&frame_lock);

  /* Create the frame element and add to the list. */
  struct frame_elem *fe = malloc (sizeof (struct frame_elem));

  if (!fe) {
    palloc_free_page (frame);
    return NULL;
  }

  fe->frame = frame;
  fe->thread = thread_current ();
  fe->spte = spte;
  spte->fe = fe;

  lock_acquire (&frame_lock);
  list_push_back (&frame_list, &fe->elem);
  lock_release (&frame_lock);

  return frame;
}

/* Deallocate(free) the frame. */
void
frame_dealloc (struct spte *spte)
{
  struct frame_elem *fe = spte->fe;

  lock_acquire (&frame_lock);
  if (fe != NULL) {
    list_remove (&fe->elem);
    palloc_free_page (fe->frame);
    free (fe);
    spte->fe = NULL;
  }
  lock_release (&frame_lock);
}

/* Evict a frame, and return new frame. */
static void *
frame_evict (enum palloc_flags flag)
{
  struct list_elem *elem = NULL;
  struct list_elem *end = list_end (&frame_list);
  struct frame_elem *fe = NULL;
  struct thread *t = NULL;

  /* Found a frame which will be evicted by LRU (Second Chance). */
  for (elem = list_begin (&frame_list);
       elem != end; elem = list_next (elem)) {
    fe = list_entry (elem, struct frame_elem, elem);
    t = fe->thread;

    if (pagedir_is_accessed (t->pagedir, fe->spte->upage)) {
      /* If the frame is accessed recently, reset the frame. */
      pagedir_set_accessed (t->pagedir, fe->spte->upage, false);
    } else {
      break;
    }
  }

  if (elem == end)
    fe = list_entry (list_begin (&frame_list), struct frame_elem, elem);

  /* Evict the frame chosen. */
  if (pagedir_is_dirty (fe->thread->pagedir, fe->spte->upage)) {
    // TODO what about a file?
    if (!swap_out (fe->spte, fe->frame)) {
      return NULL;
    }
  }

  struct spte *spte = fe->spte;
  t = fe->thread;

  pagedir_clear_page (t->pagedir, spte->upage);
  list_remove (&fe->elem);
  palloc_free_page (fe->frame);
  free (fe);

  spte->fe = NULL;

  /* Allocate a new frame. */
  return palloc_get_page (flag);
}
