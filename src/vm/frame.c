#include "threads/malloc.h"
#include "vm/frame.h"

/* Initialize the elements for frame. */
void
frame_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_list);
}

/* Allocate a frame for upage and add it to frame table(list). */
void *
frame_alloc (enum palloc_flags flag)
{
  void *frame = palloc_get_page (flag);

  if (frame != NULL) {
    /* Create the frame element and add to the list. */
    struct frame_elem *fe = malloc (sizeof (struct frame_elem));

    if (!fe)
      return NULL;

    fe->frame = frame;
    fe->thread = thread_current ();

    lock_acquire (&frame_lock);
    list_push_back (&frame_list, &fe->elem);
    lock_release (&frame_lock);
  } else {
    //TODO Evict the frame from the frame table.
  }

  return frame;
}

/* Deallocate(free) the frame. */
void
frame_dealloc (void *frame)
{
  struct list_elem *elem;
  struct list_elem *end = list_end (&frame_list);
  struct frame_elem *fe = NULL;

  lock_acquire (&frame_lock);

  for (elem = list_begin (&frame_list);
       elem != end; elem = elem->next) {
    fe = list_entry (elem, struct frame_elem, elem);

    if (fe->frame == frame) {
      list_remove (elem);
      palloc_free_page (frame);
      free (fe);
      break;
    }
  }

  lock_release (&frame_lock);
}
