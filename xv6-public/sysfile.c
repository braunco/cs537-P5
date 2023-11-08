//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "vm.h"
#include "memlayout.h"
#include "mmap.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

int find_next_mmap(struct mmap mmaps[], int req_pages) {
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

  // cprintf("Sorted Array:\n");
  // for(int i=0; i<MAX_MMAPS; i++)
  // {
  //   cprintf("%d: %x\n", i, sorted[i]->va);
  // }
  // cprintf("\n");

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

    //cprintf("Trying slot %d/%d, start=%p, end=%p, space=%d, need=%d\n", j, j+1, thisSlotStart, thisSlotEnd, spaceInSlot, req_pages * PGSIZE);

    if(spaceInSlot >= (req_pages * PGSIZE)) {
      //cprintf("taking this slot\n");
      return thisSlotStart;
    }
  }
  
  return -1;
}


//void *mmap(void *addr, int length, int prot, int flags, int fd, int offset)
int
sys_mmap(void) {
  // TODO: IMPLEMENTATION HERE
  //cprintf("entered the sys_mmap() function call\n");
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
    int next_addr = find_next_mmap(curproc->mmaps, num_pages);
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
  }

  //file backed mapping
  if(!(flags & MAP_ANONYMOUS)) {
    
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
  
  //cprintf("made the struct\n");
  
  //cprintf("initialized values\n");

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
  return 0;
}