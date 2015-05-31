#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include "devices/block.h"
#include "vm/page.h"

void swap_init (void);
void swap_destroy (void);
bool swap_out (struct spte *, void *);
void swap_in (struct spte *, void *);
void swap_free (struct spte *);

#endif /* vm/swap.h */
