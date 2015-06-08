#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <inttypes.h>
#include "devices/block.h"
#include "threads/thread.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/frame.h"

#define STACK_MAX (1 << 23)

typedef int mapid_t;

/* Supplement Page Table Entry Structure. */
struct spte
{
  struct hash_elem elem;    /* Hash element. */
  struct file *file;        /* File which has data. */
  off_t ofs;                /* Offset of the file. */
  uint8_t *upage;           /* User page address */
  uint32_t read_bytes;      /* Bytes should be read */
  uint32_t zero_bytes;      /* Rest bytes */
  bool writable;            /* For read only file. */
  bool mmap;                /* Is this page for mmap? */

  struct thread *t;         /* The thread information. */
  struct frame_elem *fe;    /* The frame information. */
  struct list_elem melem;   /* The list element for mmap. */
  struct list_elem felem;   /* The list element for frame. */

  block_sector_t swap_slot; /* The index of swap slot.
                               A value of block sector. */
  bool swap;                /* Whether the spte's data in
                               swap slot. */
};

/* List element for mmap list. */
struct mmap_elem
{
  struct list_elem elem;    /* List element. */
  struct list mlist;        /* The list for the pages of
                               mmap. */
  mapid_t id;               /* Id of mmap. */
};

unsigned page_hash_func (const struct hash_elem *, void *);
bool page_less_func (const struct hash_elem *,
                               const struct hash_elem *, void *);
void page_action_func (struct hash_elem *, void *);

struct spte * page_get_spte (void *);
struct spte * page_add_to_spte (struct file *, off_t, uint8_t *, uint32_t,
                                uint32_t, bool);
struct spte * page_add_mmap_spte (struct file *, off_t, uint8_t *,
                                  uint32_t, uint32_t,
                                  struct mmap_elem *);
bool page_load_from_spt (void *);
bool page_grow_stack (void *);

#endif /* vm/page.h */
