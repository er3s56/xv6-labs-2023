// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int id = 0; id < NCPU; id++)
    initlock(&kmem[id].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void *steal_memory(int to_id, int from_id)
{
  struct run *fast = 0;
  struct run *slow = 0;
  struct run *r = 0;
  if (to_id == from_id || kmem[to_id].freelist != 0)
    panic("steal_memory");

  int min = to_id < from_id ? to_id : from_id;
  int max = to_id < from_id ? from_id: to_id;

  acquire(&kmem[min].lock);
  acquire(&kmem[max].lock);
    
  slow = kmem[from_id].freelist;
  fast = kmem[from_id].freelist;
  kmem[to_id].freelist = slow;
  if (slow == 0)
  {
    release(&kmem[max].lock);
    release(&kmem[min].lock);
    return 0;
  }
  while (fast != 0 && fast->next != 0)
  {
    slow = slow->next;
    fast = fast->next->next;
  }

  kmem[from_id].freelist = slow->next;
  slow->next = 0;
  r = kmem[to_id].freelist;
  if (r)
    kmem[to_id].freelist = r->next;
    
  release(&kmem[max].lock);
  release(&kmem[min].lock);
  return r;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();
  
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  if (r == 0)
  {
    for (int i = 0; i < NCPU; i++)
    {
      if (i != id)
      {
        r = steal_memory(id, i);
        if (r != 0)
          break;
      }
    }
  }
  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
