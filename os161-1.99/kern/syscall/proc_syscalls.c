#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include "opt-A2.h"

#if OPT_A2
/* process related stuff */


static void save_process_status(struct proc* new, struct proc* parent) {
  struct process_status* ps = kmalloc(sizeof(struct process_status));
  ps->pid = new->pid;
  ps->parent = parent->pid;
  ps->valid = true;
  ps->process = new;
  new->ps = ps;
  array_add(process_table, ps, NULL);
}

static struct process_status* get_process_status(pid_t pid) {
  unsigned len = array_num(process_table);
  for (unsigned i = 0; i < len; i++) {
    struct process_status* ps = array_get(process_table, i); 
    // wake parents
    if (ps && ps->pid == pid) {
      return ps;
    }
  }
  return NULL;
}

#endif
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
#if OPT_A2
  lock_acquire(lk_process_table);
  // remove process status
  for (unsigned i = 0; i < array_num(process_table); i++) {
    struct process_status* ps = array_get(process_table, i); 
    KASSERT(ps);
    if (ps->pid == curproc->pid) {
      // wake parents
      KASSERT(ps->process);
      cv_broadcast(ps->process->cv_waitpid, lk_process_table);
      // set self
      ps->exitcode = _MKWAIT_EXIT(exitcode);
      ps->valid = false;
      ps->process = NULL;
    }
    // remove all children
    if (ps->parent == curproc->pid) {
      kfree(ps);
      array_remove(process_table, i);
    }
  }
  lock_release(lk_process_table);
#else
  (void)exitcode;
#endif

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
  *retval = curproc->pid;
  return 0;
#else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
#endif
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
      userptr_t status,
      int options,
      pid_t *retval)
{
#if OPT_A2
  int err = 0;
  (void)options;
  lock_acquire(lk_process_table);
  struct process_status* st = get_process_status(pid);
  // process exists
  if (st) {
    // if process is a child
    if (st->parent == curproc->pid) {
      // if process is running
      while (st->valid) {
        cv_wait(curproc->cv_waitpid, lk_process_table);
      }
      // child exits
      err = copyout(&(st->exitcode),status,sizeof(int));
      if (err) {return err;}
      *retval = st->pid;
      lock_release(lk_process_table);
      return 0;
    }
    // if process isn't a child
    lock_release(lk_process_table);
    return -1;
  }
  // process doesn't exist
  lock_release(lk_process_table);
  return -1;    
#else
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  
  return(0);
#endif
}


#if OPT_A2
/* Fork related stuff */

int sys_fork(struct trapframe* tf, pid_t* rv) {
  struct proc* proc = proc_create_runprogram(curproc->p_name);
  if (! proc) {
    return -1;
  }
  spinlock_acquire(&curproc->p_lock);
  if (as_copy(curproc->p_addrspace, &proc->p_addrspace)) {
    proc_destroy(proc);
    spinlock_release(&curproc->p_lock);
    return -1;
  }
  if (thread_fork(proc->p_name, proc, &enter_forked_process, tf, 1)) {
    proc_destroy(proc);
    spinlock_release(&curproc->p_lock);
    return -1; 
  };
  *rv = proc->pid;
  spinlock_release(&curproc->p_lock);
  lock_acquire(lk_process_table);
  save_process_status(proc, curproc);
  lock_release(lk_process_table);
  return 0;
}

#endif
