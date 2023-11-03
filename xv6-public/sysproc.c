#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "mmap.h"
#include "vm.h"



int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


//void *mmap(void *addr, int length, int prot, int flags, int fd, int offset)
int
sys_mmap(void) {
  // TODO: IMPLEMENTATION HERE
  cprintf("entered the sys_mmap() function call\n");
  int addrInt, length, prot, flags, fd, offset;
  void* addr;
  
  
  if(argint(0, &addrInt) < 0) {
    cprintf("error with addr int\n");
    return -1;
  }

  addr = (void*) addrInt;

  if(argint(1, &length) < 0) {
    cprintf("error length\n");
    cprintf("length=%d\n", length);
    return -1;
  }
  if (argint(2, &prot) < 0) {
    cprintf("error prot\n");
    cprintf("prot=%d\n", prot);
    return -1;
  }
  if (argint(3, &flags) < 0) {
    cprintf("error flags\n");
    cprintf("flags=%d\n", flags);
    return -1;
  }
  if (argint(4, &fd) < 0) {
    cprintf("error fd\n");
    cprintf("fd=%d\n", fd);
    return -1;
  }
  if (argint(5, &offset) < 0) {
    cprintf("error offset\n");
    cprintf("offset=%d\n", offset);
    return -1;
  }
  
  //char* charAddr;
  // if(argptr(0, &charAddr, sizeof(void*)) < 0) {
  //   return -1;
  // }
  if(argint(0, &addrInt) < 0) {
    cprintf("error with addr int\n");
    return -1;
  }

  if((int)addr % PGSIZE != 0) { //I THINK ITS OK TO BE INT SINCE FROM 0x60... to 0x80... (AND NOT UNSIGNED) 
    cprintf("error with pgsize\n");
    return -1;
  }

  cprintf("addrInt=%d, addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d\n", addrInt, addr, length, prot, flags, fd, offset);
  

  struct proc *curproc = myproc();
  void *start_addr = (void*)PHYSTOP;
  void *end_addr = (void*)KERNBASE;

  if (flags & MAP_FIXED) {

    start_addr = addr;
    end_addr = addr + PGROUNDUP(length);
    cprintf("end addr=%d\n", end_addr);
    if(walkpgdir(curproc->pgdir, start_addr, 0) != 0)
    {
      cprintf("fixed set and already mapped\n");
      return -1;
    }
  } else {
    // Find a free region in the address space
    // TODO: Implement the logic to find a free region
  }

  if (start_addr >= end_addr) {
    return -1;
  }

  // Update the process's page table
  // TODO: Implement the logic to update the page table lazily
  // Calculate the number of pages needed
  int num_pages = PGROUNDUP(length) / PGSIZE;

  // Allocate physical memory and update page table
  char *mem;
  for (int i = 0; i < num_pages; i++) {
    cprintf("Entering allocation for loop\n");
    mem = kalloc();
    if (mem == 0) {
      cprintf("kalloc failed\n");
      return -1;
    }
    memset(mem, 0, PGSIZE);

    if (mappages(curproc->pgdir, start_addr + (i * PGSIZE), PGSIZE, V2P(mem), PTE_W|PTE_U) < 0) {
      cprintf("mappages failed\n");
      kfree(mem);
      return -1;
    }
  }


  // Store the mapping information
  struct mmap *mmap_entry = &curproc->mmaps[curproc->num_mmaps++];
  cprintf("made the struct\n");
  mmap_entry->va = start_addr;
  mmap_entry->flags = flags;
  mmap_entry->length = PGROUNDUP(length);
  cprintf("initialized values\n");

  return (int)start_addr; // I think this is the correct cast
}

//int munmap(void *addr, size_t length)
int
sys_munmap(void)
{
  int addrInt, length;
  void* addr;

  struct proc *currProc = myproc();

  if(argint(0, &addrInt) < 0) {
    cprintf("unmap addr error\n");
    return -1;
  }
  addr = (void*)PGROUNDDOWN(addrInt);

  if(argint(1, &length) < 0) {
    cprintf("error munmap length\n");
    cprintf("length=%d\n", length);
    return -1;
  }

  int numpages = PGROUNDUP(length) / PGSIZE;

  pte_t *pte;
  //pde_t *pde;

  for(int i=0; i<numpages; i++)
  {
    pte = walkpgdir(currProc->pgdir, addr + (i * PGSIZE), 0);
    cprintf("\t\tpte %d: %d\n", i, *pte);

    //pde = &currProc->pgdir[PDX(addr + (i * PGSIZE))];

    if(*pte & PTE_P)
    {
      char* pAddr = P2V(PTE_ADDR(*pte));
      kfree(pAddr);
      *pte &= ~PTE_P;
    }
  }

  return 0;
}