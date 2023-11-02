#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "mmap.h"

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
  int addrInt, length, prot, flags, fd, offset;
  void* addr;
  
  //char* charAddr;
  // if(argptr(0, &charAddr, sizeof(void*)) < 0) {
  //   return -1;
  // }
  if(argint(0, &addrInt) < 0) {
    return -1;
  }
  addr = (void*) addrInt;
  if((int)addr % PGSIZE != 0) { //I THINK ITS OK TO BE INT SINCE FROM 0x60... to 0x80... (AND NOT UNSIGNED) 
    return -1;
  }

  if(argint(1, &length) <= 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0
  || argint(4, &fd) < 0 || argint(5, &offset) < 0) {
    return -1;
  }

  if ((flags & MAP_ANONYMOUS) && fd != -1) {
    return (void*)-1;
  }

  if ((flags & MAP_FIXED) && addr == 0) {
    return (void*)-1;
  }

  struct proc *curproc = myproc();
  void *start_addr = (void*)PHYSTOP;
  void *end_addr = (void*)KERNBASE;

  if (flags & MAP_FIXED) {
    start_addr = addr;
    end_addr = addr + PGROUNDUP(length);
  } else {
    // Find a free region in the address space
    // TODO: Implement the logic to find a free region
  }

  if (start_addr >= end_addr) {
    return (void*)-1;
  }

  // Update the process's page table
  // TODO: Implement the logic to update the page table lazily

  // Store the mapping information
  struct mmap *mmap_entry = &curproc->mmaps[curproc->num_mmaps++];
  mmap_entry->va = start_addr;
  mmap_entry->flags = flags;
  mmap_entry->length = PGROUNDUP(length);

  return (int)start_addr; // I think this is the correct cast
}