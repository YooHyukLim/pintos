#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <inttypes.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

/* Supplement Page Table Entry Structure. */
struct spte
{
  struct hash_elem elem;
  struct file *file;
  off_t ofs;
  uint8_t *upage;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
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
