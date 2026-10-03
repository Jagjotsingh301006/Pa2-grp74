#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

struct procinfo {
  int pid;
  int ppid;
  uint64 sz;
  char name[16];
};
#include "vm.h"
extern struct proc proc[NPROC];
extern struct spinlock wait_lock;
uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
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
uint64
sys_activecount(void)
{
  int count = 0;
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);

    if(p->state != UNUSED)
      count++;

    release(&p->lock);
  }

  return count;
}

// Return the number of timer ticks elapsed since boot.
uint64
sys_getuptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Print the process ancestry starting at pid and walking up to init (PID 1).
// wait_lock protects the parent pointers, so we hold it for the whole walk.
// At most one process lock is held at a time. The pid and name of every
// ancestor are copied into local arrays (safestrcpy) and printed only after
// all locks have been released.
// Returns the number of ancestors printed (including the process itself),
// or -1 if pid is not an active process.
uint64
sys_lineage(void)
{
  int pid;
  struct proc *p;
  struct proc *cur = 0;
  struct proc *parent;
  int count = 0;
  int pids[NPROC];
  char names[NPROC][16];

  argint(0, &pid);
  if (pid <= 0)
    return -1;

  acquire(&wait_lock);

  // Find the starting process while holding only this process's lock.
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      cur = p;
      break;
    }
    release(&p->lock);
  }

  if (cur == 0) {
    release(&wait_lock);
    return -1;
  }

  // Walk up the chain. cur->lock is held at the top of every iteration.
  while (count < NPROC) {
    pids[count] = cur->pid;
    safestrcpy(names[count], cur->name, sizeof(names[count]));
    count++;

    parent = cur->parent;
    release(&cur->lock);

    if (pids[count - 1] == 1 || parent == 0)
      break; // reached init, or the chain is broken

    cur = parent;
    acquire(&cur->lock);
    if (cur->state == UNUSED) { // parent is gone: do not walk further
      release(&cur->lock);
      break;
    }
  }

  release(&wait_lock);

  for (int i = 0; i < count; i++)
    printk("PID %d: %s\n", pids[i], names[i]);

  return count;
}

// Return the virtual address-space size (bytes) of an active process,
// or -1 if the pid does not exist or the process is UNUSED.
uint64
sys_getprocsize(void)
{
  int pid;
  struct proc *p;

  argint(0, &pid);

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      uint64 sz = p->sz; // read before releasing the lock
      release(&p->lock);
      return sz;
    }
    release(&p->lock);
  }

  return -1;
}

// Return the number of active direct children of a process.
// The supplied test.c calls getfamilyheadcount(mypid), while the problem
// statement says getfamilyheadcount(void). Both work: if the argument is
// not a valid pid (<= 0) the calling process is used.
// Zombies (exited but not yet reaped) and UNUSED slots are not counted.
uint64
sys_getfamilyheadcount(void)
{
  int pid;
  struct proc *p;
  struct proc *parent = 0;
  int count = 0;

  argint(0, &pid);

  acquire(&wait_lock);

  if (pid <= 0) {
    parent = myproc();
  } else {
    // Resolve the requested parent, one process lock at a time.
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->pid == pid && p->state != UNUSED) {
        parent = p;
        release(&p->lock);
        break;
      }
      release(&p->lock);
    }
  }

  if (parent == 0) {
    release(&wait_lock);
    return -1;
  }

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->state != ZOMBIE && p->parent == parent)
      count++;
    release(&p->lock);
  }

  release(&wait_lock);
  return count;
}

// Bonus (pstree): copy information about active processes to user space.
// Returns the number of entries copied, or -1 on error.
// Each entry is filled in under the locks, but copyout() is only called
// after the locks are released, because copyout may fault or allocate.
uint64
sys_getprocs(void)
{
  uint64 uaddr;
  int max;
  struct proc *p;
  struct proc *me = myproc();
  struct procinfo info;
  int count = 0;

  argaddr(0, &uaddr);
  argint(1, &max);

  if (max <= 0)
    return -1;

  for (p = proc; p < &proc[NPROC] && count < max; p++) {
    acquire(&wait_lock); // protects p->parent
    acquire(&p->lock);

    if (p->state == UNUSED || p->state == ZOMBIE) {
      release(&p->lock);
      release(&wait_lock);
      continue;
    }

    info.pid = p->pid;
    info.ppid = p->parent ? p->parent->pid : 0;
    info.sz = p->sz;
    safestrcpy(info.name, p->name, sizeof(info.name));

    release(&p->lock);
    release(&wait_lock);

    if (copyout(me->pagetable, me->sz, uaddr + count * sizeof(info),
                (char *)&info, sizeof(info)) < 0)
      return -1;

    count++;
  }

  return count;
}
