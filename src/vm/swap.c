#include <bitmap.h>
#include "devices/block.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

#define SWAP_CNT (PGSIZE/BLOCK_SECTOR_SIZE)
#define FREE 0
#define USED 1

struct bitmap *swap_bitmap;  /* Bitmap for Swap slot. */
struct block *swap_block;     /* Block for Swapping. */
struct lock swap_lock;       /* Lock for Swapping. */

/* Initialize Swap. */
void
swap_init ()
{
  /* Get the information of blocks for Swap. */
  swap_block = block_get_role (BLOCK_SWAP);

  if (!swap_block) {
    swap_bitmap = NULL;
    return;
  }

  /* Create a bitmap for checking swap slots available. */
  swap_bitmap = bitmap_create (SWAP_CNT);

  if (!swap_bitmap)
    return;

  /* Set all swap slot free(0). */
  bitmap_set_all (swap_bitmap, FREE);

  /* Initiate the lock for synchronization. */
  lock_init (&swap_lock);
}

/* Destroy all elements for swap. */
void
swap_destroy ()
{
  bitmap_destroy (swap_bitmap);
}

//bool
//swap_out (struct frame_elem *frame)
//{
//}
//
//void
//swap_in ()
//{
//}
