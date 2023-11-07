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


  if((int)addr % PGSIZE != 0) { //I THINK ITS OK TO BE INT SINCE FROM 0x60... to 0x80... (AND NOT UNSIGNED) 
    cprintf("error with pgsize\n");
    return -1;
  }

  cprintf("addrInt=%d, addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d\n", addrInt, addr, length, prot, flags, fd, offset);

  struct proc *curproc = myproc();
  void *start_addr = (void*)PHYSTOP;
  void *end_addr = (void*)KERNBASE;

  if(curproc->num_mmaps >= 32) {
    cprintf("too many maps\n");
    return -1;
  }

  if (flags & MAP_FIXED) {

    if(addrInt < MMAPVIRTBASE || addrInt > KERNBASE) {
      return -1;
    }

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
  struct mmap *mmap_entry = 0; //= &curproc->mmaps[curproc->num_mmaps++];
  int i;
  for(i = 0; i < MAX_MMAPS; i++) {
    if(curproc->mmaps[i].va == 0) {
      mmap_entry = &curproc->mmaps[i];
      break;
    }
  }
  mmap_entry->va = start_addr;
  mmap_entry->prot = prot;
  mmap_entry->flags = flags;
  mmap_entry->fd = fd;
  mmap_entry->length = PGROUNDUP(length);
  mmap_entry->offset = offset;
  cprintf("made the struct\n");
  
  cprintf("initialized values\n");

  return (int)start_addr; // I think this is the correct cast
}

//int munmap(void *addr, size_t length)
int
sys_munmap(void)
{
  int addrInt, length;
  void* addr;

  // input parameter checks
  if(argint(0, &addrInt) < 0) {
    cprintf("unmap addr error\n");
    return -1;
  }
  if(argint(1, &length) < 0) {
    cprintf("error munmap length\n");
    cprintf("length=%d\n", length);
    return -1;
  }

  // Convert void* and round down to the nearest page boundary
  addr = (void*)PGROUNDDOWN(addrInt);
  //cprintf("addr=%p\n", addr);
  length = PGROUNDUP(length); // round length up to the nearst page boundary

  // Calculate the # of pages to unmap
  int numpages = length / PGSIZE;
  if(numpages <=0) {
    cprintf("Num pages = %d", numpages);
    return -1; //invalid length
  }

  struct proc *currProc = myproc();

  for(int i=0; i<numpages; i++)
  {
    // Calcualte the address of the page to unmap
    void *pageAddr = addr + (i * PGSIZE);

    // Get the page table entry for the page
    pte_t *pte = walkpgdir(currProc->pgdir, pageAddr, 0);
    if(pte && (*pte & PTE_P)) {
      // Free the physical mem if the page is present
      cprintf("Before clearing PTE_P, pte[%d] = %x\n", i, *pte);
      char *pAddr = P2V(PTE_ADDR(*pte));
      kfree(pAddr);

      // Clear the page table entry
      //*pte &= ~PTE_P;
      cprintf("After clearing PTE_P, pte[%d] = %x pgdir = %x\n", i, *pte);

      pde_t *pde = &currProc->pgdir[PDX(addr)];
      cprintf("pde before: %x\n", *pde);
      *pde &= ~PTE_P;
      cprintf("pde after: %x\n", *pde);

    } else {
      cprintf("PTE note present for the page %d\n", i);
    }

  }

  // Still have to remove the mmap entry from the struct - not implemented yet

  struct mmap *mmap_entry = 0; //= &curproc->mmaps[curproc->num_mmaps++];
  int i;
  for(i = 0; i < MAX_MMAPS; i++) {
    if(currProc->mmaps[i].va == addr) {
      mmap_entry = &currProc->mmaps[i];
      memset((void*)mmap_entry, 0, sizeof(struct mmap));
      break;
    }
  }

  // struct proc *p = myproc();
  // struct mmap *curmap;
  // for(int i = 0; i < 32; i++) {
  //   curmap = &p->mmaps[i];
  //   cprintf("[%d]th mmap virtual address: %p, and length: %d\n", i, curmap->va, curmap->length);
  // }
  return 0;
}

/*
int sys_mmap(void) {
  int addr, length, offset;
  int prot, flags, fd;
  argaddr(0, &addr);
  argaddr(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);
  int ret;
  if((ret = (int)mmap((void*)addr, length, prot, flags, fd, offset)) < 0){
    printf("[Kernel] fail to mmap.\n");
    return -1;
  }
  return ret;
}

int sys_munmap(void){
  int addr, length;
  argaddr(0, &addr);
  argaddr(1, &length);
  int ret;
  if((ret = munmap((void*)addr, length)) < 0){
    return -1;
  }
  return ret;
}
*/