#include <stdio.h>
#include <string.h>
#include <hash.h>
#include <debug.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/frame.h"

/* Get the Hash value of the user page. */
unsigned
page_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct spte *spte = hash_entry (e, struct spte, elem);
  return hash_int ((int) spte->upage);
}

/* Hash less function for hash table. */
bool
page_less_func (const struct hash_elem *a, const struct hash_elem *b,
                void *aux UNUSED)
{
  struct spte *spte_a = hash_entry (a, struct spte, elem);
  struct spte *spte_b = hash_entry (b, struct spte, elem);
  return spte_a->upage < spte_b->upage ? true : false;
}

/* Hash action function for destroying of hash.
   At here, if there is a mapping page of upage,
   then clear and free it. */
void
page_action_func (struct hash_elem *e, void *aux UNUSED)
{
  void *frame;
  struct spte *spte = hash_entry (e, struct spte, elem);

  if ((frame = pagedir_get_page (thread_current ()->pagedir,
                                 (const void *) spte->upage))
      != NULL) {
    frame_dealloc (frame);
    pagedir_clear_page (thread_current ()->pagedir,
                        spte->upage);
  }
  free (spte);
}


/* Get the proper supplement page table entry from the hash. */
struct spte *
page_get_spte (void *upage)
{
  struct spte spte;
  struct hash_elem *elem;
  spte.upage = pg_round_down (upage);
  elem = hash_find (&thread_current ()->spt, &spte.elem);

  if (!elem)
    return NULL;
    
  return hash_entry (hash_find (&thread_current ()->spt, &spte.elem),
                     struct spte,
                     elem);
}

/* Add the page which has to be loaded to
   the supplement page table entry. */
bool
page_add_to_spte (struct file *file, off_t ofs, uint8_t *upage,
                 uint32_t read_bytes, uint32_t zero_bytes,
                 bool writable)
{
  struct spte *spte = (struct spte *) malloc (sizeof (struct spte));
  
  if (!spte)
    return false;

  spte->file = file;
  spte->ofs = ofs;
  spte->upage = upage;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  spte->writable = writable;

  if (hash_insert (&thread_current ()->spt, &spte->elem) != NULL) {
    free (spte);
    return false;
  }

  return true;
}

/* Load proper page from the information of Supplement page table. */
bool
page_load_from_spt (void *upage)
{
  /* Get a frame of memory. */
  struct spte *spte = page_get_spte (upage);

  if (!spte)
    return false;

  void *frame = frame_alloc (spte, PAL_USER);
  struct thread *t = thread_current ();
  
  if (!frame)
    return false;

  /* Load this page. */
  lock_acquire (&filesys_lock);
  if (file_read_at (spte->file, frame, spte->read_bytes, spte->ofs)
      != (int) spte->read_bytes) {
    lock_release (&filesys_lock);
    frame_dealloc (frame);
    return false; 
  }
  lock_release (&filesys_lock);
  memset (frame + spte->read_bytes, 0, spte->zero_bytes);

  /* Add the page to the process's address space. */
  if (pagedir_get_page (t->pagedir, spte->upage) != NULL
      || !pagedir_set_page (t->pagedir, spte->upage,
                            frame, spte->writable)) {
    frame_dealloc (frame);
    return false; 
  }

  return true;
}
