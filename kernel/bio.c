// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define HASHMAP_SIZE 13
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
  struct spinlock hashlock[HASHMAP_SIZE];
  struct buf bhashmap[HASHMAP_SIZE];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < HASHMAP_SIZE; i++)
  {
    initlock(&bcache.hashlock[i], "bcache.bucket");
    bcache.bhashmap[i].next = &bcache.bhashmap[i];
    bcache.bhashmap[i].prev = &bcache.bhashmap[i];
  }

  for (int i = 0; i < NBUF; i++)
  {
    int idx = i % HASHMAP_SIZE;
    initsleeplock(&bcache.buf[i].lock, "buffer");
    bcache.buf[i].next = bcache.bhashmap[idx].next;
    bcache.buf[i].prev = &bcache.bhashmap[idx];
    bcache.bhashmap[idx].next->prev = &bcache.buf[i];
    bcache.bhashmap[idx].next = &bcache.buf[i];
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *find = 0;
  int idx = blockno % HASHMAP_SIZE;

  acquire(&bcache.hashlock[idx]);
  for (b = bcache.bhashmap[idx].next; b != &bcache.bhashmap[idx]; b = b->next)
  {
    if (b->refcnt == 0)
      find = b;

    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.hashlock[idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  if (find)
  {
    find->dev = dev;
    find->blockno = blockno;
    find->valid = 0;
    find->refcnt = 1;
    release(&bcache.hashlock[idx]);
    acquiresleep(&find->lock);
    return find;
  }

  release(&bcache.hashlock[idx]);

  for (int i = 0; i < HASHMAP_SIZE; i++)
  {
    if (i == idx)
      continue;

    int min = idx < i ? idx : i;
    int max = idx < i ? i : idx;
    acquire(&bcache.hashlock[min]);
    acquire(&bcache.hashlock[max]);
    for (b = bcache.bhashmap[i].next; b != &bcache.bhashmap[i]; b = b->next)
    {
      if (b->refcnt == 0)
      {
        b->prev->next = b->next;
        b->next->prev = b->prev;
        b->next = bcache.bhashmap[idx].next;
        b->prev = &bcache.bhashmap[idx];
        bcache.bhashmap[idx].next->prev = b;
        bcache.bhashmap[idx].next = b;
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.hashlock[max]);
        release(&bcache.hashlock[min]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.hashlock[max]);
    release(&bcache.hashlock[min]);
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int idx = b->blockno % HASHMAP_SIZE;

  acquire(&bcache.hashlock[idx]);
  b->refcnt--;
  if (b < 0)
    panic("less than 0");
  release(&bcache.hashlock[idx]);
}

void
bpin(struct buf *b) {
  int idx = b->blockno % HASHMAP_SIZE;
  acquire(&bcache.hashlock[idx]);
  b->refcnt++;
  release(&bcache.hashlock[idx]);
}

void
bunpin(struct buf *b) {
  int idx = b->blockno % HASHMAP_SIZE;
  acquire(&bcache.hashlock[idx]);
  b->refcnt--;
  release(&bcache.hashlock[idx]);
}


