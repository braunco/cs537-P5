#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "mmap.h"
#include "vm.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;


static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // Initialize mmaps array and num_mmaps field
  for (int i = 0; i < MAX_MMAPS; i++) {
    p->mmaps[i].va = 0;  // Initialize virtual address to 0
    p->mmaps[i].flags = 0;  // Initialize flags to 0
    p->mmaps[i].length = 0;  // Initialize length to 0
  }
  p->num_mmaps = 0;  // Initialize the number of memory mappings to 0

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  for(int i=0; i<MAX_MMAPS; i++) {
    struct mmap *currMap = &curproc->mmaps[i];

    if(currMap->flags & MAP_SHARED) {
      //TODO: prob need to go through all pages in this mapping

      //do_mmap((int)currMap->va, currMap->length, currMap->prot, currMap->flags, currMap->fd, currMap->offset, currMap->fp, np);
      pte_t *parentPTE = walkpgdir(curproc->pgdir, currMap->va, 0);
      uint physAddr = PTE_ADDR(*parentPTE);
      physAddr = (uint)P2V(physAddr);
      cprintf("mem new: %p, %p\n", physAddr, *parentPTE);
      if(mappages(np->pgdir, currMap->va, PGSIZE, V2P(physAddr), PTE_W|PTE_U) < 0) {
        cprintf("map pages copy fail\n");
      }
      np->mmaps[i].isChild = 1;
    } 
    else if (currMap->flags & MAP_PRIVATE) { //if not map shared, its map private.
      //assuming previous mappings are done correctly, add on map_fixed when calling mmap to get right spot in mem
      //TODO: something with files is different
      do_mmap((int)currMap->va, currMap->length, currMap->prot, currMap->flags | MAP_FIXED, currMap->fd, currMap->offset, currMap->fp, np);
      pte_t *parentPTE = walkpgdir(curproc->pgdir, currMap->va, 0);
      uint parentPhysAddr = PTE_ADDR(*parentPTE);
      pte_t *newPte = walkpgdir(np->pgdir, currMap->va, 0);
      uint newPhysAddr = PTE_ADDR(*newPte);
      memmove(P2V(newPhysAddr), P2V(parentPhysAddr), PGSIZE);
    }

    np->mmaps[i].fd = curproc->mmaps[i].fd;
    np->mmaps[i].flags = curproc->mmaps[i].flags;
    np->mmaps[i].fp = curproc->mmaps[i].fp;
    np->mmaps[i].length = curproc->mmaps[i].length;
    np->mmaps[i].offset = curproc->mmaps[i].offset;
    np->mmaps[i].prot = curproc->mmaps[i].prot;
    np->mmaps[i].va = curproc->mmaps[i].va;

  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  //remove all mappings that have another reference so OS doesn't free
  for(int i=0; i<MAX_MMAPS; i++) {
    struct mmap* currMap = &curproc->mmaps[i];
    if(currMap->isChild && currMap->flags & MAP_SHARED) { //if shared
      do_munmap((int)currMap->va, currMap->length);
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int find_next_mmap(struct mmap mmaps[], int req_pages, int growsup_flag) {
  struct mmap *min = 0;
  struct mmap prev = (struct mmap) { (void*)(MMAPVIRTBASE - 1) };
  struct mmap *sorted[MAX_MMAPS];
  memset(&sorted, 0, sizeof(struct mmap*) * 32);

  for (int a = 0; a < MAX_MMAPS; a++) {
    sorted[a]->va = 0;
  }

  for (int i = 0; i < MAX_MMAPS; i++) {
    if(mmaps[i].va == 0) continue;
    for(int j = 0; j < MAX_MMAPS; j++) {
      if(mmaps[j].va == 0) continue;
      if ((min == 0 || mmaps[j].va < min->va) && (int)mmaps[j].va > (int)prev.va) {
        min = &mmaps[j];
      }
    }
    sorted[i] = min;
    prev.va = min->va;
    min = 0;
  }


  for(int j=0; j<MAX_MMAPS; j++)
  {
    uint thisSlotStart;

    if(sorted[j]->va == 0) {
      thisSlotStart = MMAPVIRTBASE;
    }
    else {
      thisSlotStart = (int)(sorted[j]->va + sorted[j]->length);
    }

    uint thisSlotEnd;
    //if not the last element
    if(j < MAX_MMAPS - 1 && sorted[j+1]->va != 0)
    {
      thisSlotEnd = (int)sorted[j+1]->va;
    }
    else //if last element, need to compare to end of memory instead of next element
    {
      thisSlotEnd = KERNBASE;
    }

    uint spaceInSlot = thisSlotEnd - thisSlotStart;

    if(growsup_flag) {
    cprintf("Entered grows_up flag conditional\n");
      // Check if we can grow the current mapping by one page size
      if(spaceInSlot >= PGSIZE && (thisSlotEnd - (thisSlotStart + PGSIZE)) >= PGSIZE) {

        // There is enough space to grow the mapping by one page size
        // without coming within one page size of the next mapping.
        return thisSlotStart;
      } else {
        cprintf("Handles page fault correctly\n");
        handle_page_fault();
      }
    } else { // normal case
      // Check if there is enough space for the requested number of pages
      if(spaceInSlot >= (req_pages * PGSIZE)) {
        return thisSlotStart;
      }
    }
  }
  
  return -1;
}

int do_mmap(int addrInt, int length, int prot, int flags, int fd, int offset, struct file* fp, struct proc *curproc)
{
    void* addr = (void*) addrInt;

    if((int)addr % PGSIZE != 0) { //I THINK ITS OK TO BE INT SINCE FROM 0x60... to 0x80... (AND NOT UNSIGNED) 
        cprintf("error with pgsize\n");
        return -1;
    }

  cprintf("addrInt=%d, addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d\n", addrInt, addr, length, prot, flags, fd, offset);

  //struct proc *curproc = myproc();
  void *start_addr = (void*)MMAPVIRTBASE;
  void *end_addr = (void*)KERNBASE;

  if(curproc->num_mmaps >= 32) {
    cprintf("too many maps\n");
    return -1;
  }

  int num_pages = PGROUNDUP(length) / PGSIZE;

  if (flags & MAP_FIXED) {

    if(addrInt < MMAPVIRTBASE || addrInt > KERNBASE) {
      return -1;
    }

    start_addr = addr;
    end_addr = addr + PGROUNDUP(length);
    cprintf("end addr=%d\n", end_addr);
    pte_t *pte_found;
    if((pte_found = walkpgdir(curproc->pgdir, start_addr, 0)) != 0)
    {
      if(*pte_found & PTE_P)
      {
        cprintf("fixed set and already mapped\n");
        return -1;
      }
    }
  } else {
    int next_addr = find_next_mmap(curproc->mmaps, num_pages, 0);
    //cprintf("\t%p\n", next_addr);
    if(next_addr != -1) {
      start_addr = (void*)next_addr;
    }
    else {
      return -1;
    }
  }

  cprintf("Actual address being used: %p\n", start_addr);

  if (start_addr >= end_addr) {
    return -1;
  }


  if (flags & MAP_GROWSUP) {
    // Handle the MAP_GROWSUP flag
    int new_addr = find_next_mmap(curproc->mmaps, num_pages+1, 1); // Check for one page growth
    if(new_addr != -1) {
      // Extend the mapping by one page
      num_pages++;
      // Update the length of the existing mapping
        curproc->mmaps->length += PGSIZE;
    } else {
      // Cannot extend the mapping, handle the error
      handle_page_fault();
    }
  }

  // Update the process's page table
  // TODO: Implement the logic to update the page table lazily
  // Calculate the number of pages needed
  

  // Allocate physical memory and update page table
  char *mem;
  for (int i = 0; i < num_pages; i++) {
    //cprintf("Entering allocation for loop\n");

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

    pte_t *testPTE = walkpgdir(curproc->pgdir, start_addr + (i * PGSIZE), 0);
    cprintf("mem orig: %p, %p\n", mem, *testPTE);

    //file backed mapping
    if(fp != 0) {

      if(fileread(fp, mem, PGSIZE) < 0) {
        cprintf("fileread failed\n");
      }

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
  mmap_entry->fp = fp;
  mmap_entry->isChild = 0;
  
  //cprintf("made the struct\n");
  
  //cprintf("initialized values\n");

  curproc->num_mmaps++;
  return (int)start_addr; // I think this is the correct cast
}

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};

int do_munmap(int addrInt, int length)
{
  void* addr;

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

  struct file* fp = 0;
  for(int i=0; i<MAX_MMAPS; i++) {
    if(currProc->mmaps[i].va == addr) {
      fp = currProc->mmaps[i].fp;
      break;
    }
  }

  // if(fp == 0) {
  //   cprintf("ERROR: fp not gotten\n");
  //   return -1;
  // }

  cprintf("For addr: %p\n", addr);

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

      if(fp != 0) {
        fp->off = 0;
        filewrite(fp, pAddr, PGSIZE);
      }      

      int mapIndex = 0;
      for(int i=0; i<MAX_MMAPS; i++) {
        if(currProc->mmaps[i].va == addr) {
          mapIndex = i;
        }
      }
      if(!currProc->mmaps[mapIndex].isChild)
        kfree(pAddr);

      // Clear the page table entry
      //*pte &= ~PTE_P;
      

      *pte &= ~PTE_P;
      cprintf("After clearing PTE_P, pte[%d] = %x\n", i, *pte);

    } else {
      cprintf("PTE note present for the page %d\n", i);
    }

  }

  cprintf("\n");

  // Remove the mmap entry from the struct
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
  currProc->num_mmaps--;
  return 0;
}

void handle_page_fault() 
{
  // PAGE_FAULT_HANDLER:
    // /* Case 2 - MAP_GROWSUP */
    // if guard_page:
    //   handle it

    //uint attemptedVirtAddr = rcr2(); // NEED THIS
    //pte_t *attemptedPte = 

      
    // /* Case 3 - None of the Above - Error */
    cprintf("Segmentation Fault\n");
    if(kill(myproc()->pid) < 0) {
      cprintf("ERROR: failed to kill process %d\n", myproc()->pid);
    }
    else {
      exit();
    }
}

/*
void*
mmap(void* addr, int length, int prot, int flags, int fd, int offset) {
  // At this time, an unused virtual memory space should be found from the process.
  int start_addr = find_unallocated_area(length); // Not yet implemented
  if(start_addr == 0) {
    printf("can't find area");
    return (void*) -1;
  }

  // mmaps struct
  struct mmap m;
  m.start_addr = start_addr;
  m.end_addr = start_addr + length;
  m.length = length.file = myproc()->ofile[fd];
  m.flags = flags;
  m.prot = prot;
  m.offset = offset;

  // Check if permission bits meet map requirements
  if ((m.prot & PROT_WRITE) && (m.flags == MAP_SHARED) && (!m.file->writeable)) {
    printf("[Kernel] mmap: prot is invalid.\n");
    return (void*) -1;
  }

  // Add file reference
  struct file* f = myproc()->ofile[fd];
  fileup(f);

  // Put mmaps into struct
  if(push_mmaps(m) == -1) {
    printf("[Kernel] mmap: fail to push mem area.\n");
    return (void*) -1;
  }
  return (void*)start_addr;

}

int
munmap(void* addr, int length) {
  // Find pa related to va
  struct mmap* mmaps = find_area((int)addr);

  // Briefly consider
  int start_addr = PGROUNDDOWN((int)addr);
  int end_addr = PGROUNDUP((int)addr + length);
  if (end_addr = mmaps->end_addr && start_addr == mmaps->start_addr) {
    struct file* f = mmaps->file;
    if(mmaps->flags == MAP_SHARED && mmaps->prot & PROT_WRITE) {
      // Write mem area back to file
      printf("[Kernel] start_addr: %p, length: 0x%x\n" mmaps->start_addr, mmaps->length);
      if(filewrite(f,mmaps->start_addr, mmaps->length) < 0) {
        printf("[Kernel] munmap: fail to write back file.\n");
        return -1;
      }
    }

    // Unmap and release virtual memory
    for (int i = 0; i < mmaps->length / PGSIZE; i++) {
      int addr = mmaps->start_addr + i * PGSIZE;
      int pa = walkaddr(myproc()->pagetable, addr);
      if(pa != 0) {
        uvmunmap(myproc()->pagetable, addr, PGSIZE, 1);
      }
    }

    // Remove file references
    fileclose(f);

    // Delete memory area from table
    if (rm_area(mmaps) < 0) {
      printf("[Kernel] munmap: fail to remove mem area from table.\n");
      return -1;
    }
    return 0;
  } else if (end_addr <= mmaps->end_addr && start_addr >= mmaps->start_addr) {
    // This means area is a sub-area
    struct file* f = mmaps->file;
    if(mmaps->flags = MAP_SHARED && mmaps->prot & PROT_WRITE) {
      // Write back file & get offset
      int offset = start_addr - mmaps->start_addr;
      int len = end_addr - start_addr;
      if(f->type == FD_INODE) {
        begin_op(f->ip->dev);
        ilock(f->ip);
        if(writei(f->ip, 1, start_addr, offset, len) < 0){
          printf("[Kernel] munmap: fail to write back file.\n");
          iunlock(f->ip);
          end_op(f->ip->dev);
          return -1;
        }
        iunlock(f->ip);
        end_op(f->ip->dev);
      }
      
    }
  

    // unmap and release virtual memory
    int len = end_addr - start_addr;
    for(int i = 0; i < len / PGSIZE; i++) {
      int addr = start_addr + i * PGSIZE;
      int pa = walkaddr(myproc()->pagetable, addr);
      if (pa != 0) {
        uvmumap(myproc()->pagetable, addr, PGSIZE, 1);
      }
    }

    // Modify mmaps struct
    if(start_addr == mmaps->start_addr) {
      mmaps->offset = end_addr - mmaps->start_addr;
      mmaps->start_addr = end_addr;
      mmaps->length = mmaps->end_addr - mmaps->start_addr;
    }else if(end_addr == mmaps->end_addr){
      mmaps->end_addr = start_addr;
      mmaps->length = mmaps->end_addr - mmaps->start_addr;
    } else {
      // necessary to divide into sections
      panic("[Kernel] munmap: we're not doing this\n");
    }
    return 0;
  }else if(end_addr > mmaps->end_addr){
    panic("[Kernel] munmap: out of current range.\n");
  }else{
    panic("[Kernel] munmap: unresolved solution.\n");
  }
}
*/