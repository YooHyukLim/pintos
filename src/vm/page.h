#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <inttypes.h>
#include "devices/block.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/frame.h"

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

  struct frame_elem *fe;    /* The frame information. */

  block_sector_t swap_slot; /* The index of swap slot.
                               A value of block sector. */
};

unsigned page_hash_func (const struct hash_elem *, void *);
bool page_less_func (const struct hash_elem *,
                               const struct hash_elem *, void *);
void page_action_func (struct hash_elem *, void *);

struct spte * page_get_spte (void *);
bool page_add_to_spte (struct file *, off_t, uint8_t *, uint32_t,
                       uint32_t, bool);
bool page_load_from_spt (void *);

#endif /* vm/page.h */