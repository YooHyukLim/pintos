#include <stdio.h>
#include <string.h>
#include <debug.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"

static bool page_load_from_file (struct spte *);
static bool page_load_from_swap (struct spte *);

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
    if (spte->fe != NULL && spte->fe->frame != NULL) {
      pagedir_clear_page (thread_current ()->pagedir,
                          spte->upage);
      frame_dealloc (spte);
    }

    if (spte->swap_slot != (block_sector_t) -1)
      swap_free (spte);
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
    
  return hash_entry (elem, struct spte, elem);
}

/* Add the page which has to be loaded to
   the supplement page table entry. */
struct spte *
page_add_to_spte (struct file *file, off_t ofs, uint8_t *upage,
                  uint32_t read_bytes, uint32_t zero_bytes,
                  bool writable)
{
  struct spte *spte = (struct spte *) malloc (sizeof (struct spte));
  
  if (!spte)
    return NULL;

  spte->file = file;
  spte->ofs = ofs;
  spte->upage = upage;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  spte->writable = writable;
  spte->mmap = false;
  spte->t = thread_current ();
  spte->fe = NULL;
  spte->swap_slot = (block_sector_t) -1;

  if (hash_insert (&thread_current ()->spt, &spte->elem) != NULL) {
    free (spte);
    return NULL;
  }

  return spte;
}

/* Add the page for mmap which has to be loaded to
   the supplement page table. */
struct spte *
page_add_mmap_spte (struct file *file, off_t ofs, uint8_t *upage,
                    uint32_t read_bytes, uint32_t zero_bytes,
                    struct mmap_elem *me)
{
  /* Check there is already a spt entry of upage given. */
  struct spte *spte = page_get_spte (upage);
  if (spte != NULL)
    return NULL;

  /* Add a new spt entry for mmap. */
  spte = page_add_to_spte (file, ofs, upage, read_bytes,
                           zero_bytes, true);
  if (!spte)
    return NULL;
  spte->mmap = true;

  /* Add this entry to the mmap list of cur thread. */
  list_push_back (&me->mlist, &spte->melem);

  return spte;
}

/* Load the proper page from the information
   of Supplement page table. */
bool
page_load_from_spt (void *upage)
{
  struct spte *spte = page_get_spte (upage);

  if (!spte) {
    struct thread *t = thread_current ();

    /* If the given upage is for stack, then allocate a new
       memory frame and grow the stack. */
    if ((uint8_t *) upage >= (uint8_t *) PHYS_BASE - STACK_MAX
        && (uint8_t *) upage >= t->user_stack - 32)
      return page_grow_stack (upage);
    return false;
  }

  /* Do proper loading according to the style of the spte. */
  if (spte->swap_slot == (block_sector_t) -1
      || spte->mmap) {
    return page_load_from_file (spte);
  } else
    return page_load_from_swap (spte);
}

/* Load the data from the file. */
static bool
page_load_from_file (struct spte *spte)
{
  void *frame = frame_alloc (spte, PAL_USER);
  struct thread *t = thread_current ();
  
  if (!frame) {
    return false;
  }

  /* Load this page. */
  lock_acquire (&filesys_lock);
  if (file_read_at (spte->file, frame, spte->read_bytes, spte->ofs)
      != (int) spte->read_bytes) {
    lock_release (&filesys_lock);
    frame_dealloc (spte);
    return false; 
  }
  lock_release (&filesys_lock);
  memset (frame + spte->read_bytes, 0, spte->zero_bytes);

  /* Add the page to the process's address space. */
  if (pagedir_get_page (t->pagedir, spte->upage) != NULL
      || !pagedir_set_page (t->pagedir, spte->upage,
                            frame, spte->writable)) {
    frame_dealloc (spte);
    return false; 
  }

  return true;
}

/* Load the data from the swap. */
static bool
page_load_from_swap (struct spte *spte)
{
  void *frame = frame_alloc (spte, PAL_USER);
  struct thread *t = thread_current ();

  if (!frame)
    return false;

  /* Allocate a phys page to the user page. */
  if (pagedir_get_page (t->pagedir, spte->upage) != NULL
      || !pagedir_set_page (t->pagedir, spte->upage,
                            frame, spte->writable)) {
    frame_dealloc (spte);
    return false;
  }

  /* Recover the data of the user page from the swap slot. */
  swap_in (spte, frame);
  pagedir_set_dirty (t->pagedir, spte->upage, true);

  return true;
}

/* Allocate a new frame for stack. */
bool
page_grow_stack (void *upage)
{
  struct spte *spte = page_add_to_spte (NULL, 0,
                                        pg_round_down (upage),
                                        0, 0, true);

  if (!spte)
    return false;

  /* Allocate a new frame for stack. The frame will be initialized
     by all zero. */
  void *frame = frame_alloc (spte, PAL_USER | PAL_ZERO);
  struct thread *t = thread_current ();

  if (!frame) {
    hash_delete (&t->spt, &spte->elem);
    free (spte);
    return false;
  }

  /* Allocate a phys page to the user page. */
  if (pagedir_get_page (t->pagedir, spte->upage) != NULL
      || !pagedir_set_page (t->pagedir, spte->upage,
                            frame, spte->writable)) {
    frame_dealloc (spte);
    hash_delete (&t->spt, &spte->elem);
    free (spte);
    return false;
  }

  /* The page of stack is dirty. It means when the page is evicted,
     it should be saved to swap. */
  pagedir_set_dirty (t->pagedir, spte->upage, true);

  return true;
}
