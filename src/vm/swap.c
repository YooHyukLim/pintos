#include <bitmap.h>
#include <debug.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

#define SWAP_CNT (PGSIZE/BLOCK_SECTOR_SIZE) /* The number of swaps
                                               which are according to
                                               Page Size. */

/* The value of Bitmap. */
#define FREE false         /* It represents the swap slot is free. */
#define USED true          /* It represents the swap slot is being
                              used for saving a frame. */

struct bitmap *swap_bitmap;               /* Bitmap for Swap slot. */
struct block *swap_block;                 /* Block for Swapping. */
struct lock swap_lock;                    /* Lock for Swapping. */

/* Initialize Swap. */
void
swap_init ()
{
  /* Initiate the lock for synchronization. */
  lock_init (&swap_lock);

  /* Get the information of blocks for Swap. */
  swap_block = block_get_role (BLOCK_SWAP);

  if (!swap_block) {
    swap_bitmap = NULL;
    return;
  }

  /* Create a bitmap for checking swap slots available. */
  swap_bitmap = bitmap_create (block_size (swap_block) / SWAP_CNT);

  if (!swap_bitmap)
    return;

  /* Set all swap slot free(0). */
  bitmap_set_all (swap_bitmap, FREE);
}

/* Destroy all elements for swap. */
void
swap_destroy ()
{
  bitmap_destroy (swap_bitmap);
}

/* Swap the frame to the block. (Swap out) */
bool
swap_out (struct spte *spte, void *frame)
{
  ASSERT (frame != NULL);

  size_t i;
  size_t bm_index;

  /* Get the swap slot where the frame will be set. */
  lock_acquire (&swap_lock);
  bm_index = bitmap_scan_and_flip (swap_bitmap, 0, 1, FREE);
  lock_release (&swap_lock);

  if (bm_index == BITMAP_ERROR) {
    PANIC ("Full Swap.");
    return false;
  }

  /* Get the swap slot's index. Because it is block_sector_t,
     we have to change the value which is gotten from bitmap
     to the value * SWAP_CNT. */
  spte->swap_slot = bm_index * SWAP_CNT;

  /* Write the data of the frame to the swap slot. */
  lock_acquire (&filesys_lock);
  for (i = 0; i < SWAP_CNT; i++)
    block_write (swap_block, spte->swap_slot + i,
                 frame + i * BLOCK_SECTOR_SIZE);
  lock_release (&filesys_lock);

  return true;
}

/* Get the proper data from swap slot to the frame given. */
void
swap_in (struct spte *spte, void *frame)
{
  ASSERT (frame != NULL);
  ASSERT (spte->swap_slot != (block_sector_t) -1);
  
  size_t bm_index = spte->swap_slot / SWAP_CNT;
  size_t i;

  /* Read the data from swap slot to the frame. */
  lock_acquire (&filesys_lock);
  for (i = 0; i < SWAP_CNT; i++)
    block_read (swap_block, spte->swap_slot + i,
                frame + i * BLOCK_SECTOR_SIZE);
  lock_release (&filesys_lock);

  /* Reset the swap slot index of spte to notify that the data of
     this spte was already loaded. */
  spte->swap_slot = (block_sector_t) -1;

  lock_acquire (&swap_lock);
  bitmap_set (swap_bitmap, bm_index, FREE);
  lock_release (&swap_lock);
}

/* Free the swap allocated for a process */
void
swap_free (struct spte *spte)
{
  size_t bm_index = spte->swap_slot / SWAP_CNT;

  lock_acquire (&swap_lock);
  bitmap_set (swap_bitmap, bm_index, FREE);
  lock_release (&swap_lock);
}
