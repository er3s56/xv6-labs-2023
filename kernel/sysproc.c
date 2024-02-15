#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  int n;
  uint32 abit = 0;
  uint64 buf, abit_addr;
  pte_t *pte;
  pagetable_t pagetable = myproc()->pagetable;

  argint(1, &n);
  argaddr(0, &buf);
  argaddr(2, &abit_addr);
  
  if (n > 32)
    return -1;
  pte = walk(pagetable, buf, 0);

  if (pte == 0)
    return -1;

  for (int i = 0; i < n; i++)
  {
    if((pte[i] & PTE_A) != 0)
      abit = abit | 1 << i;
  }

  if (copyout(pagetable, abit_addr, (char *)&abit, sizeof(uint32)) == -1)
    return -1;
  
  for (int i = 0; i < n; i++)
  {
    pte[i] = pte[i] & ~PTE_A;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
