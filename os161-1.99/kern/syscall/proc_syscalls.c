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
#include <mips/trapframe.h>
#include "debug.h"
#include "limits.h"
#endif 

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

   struct addrspace *as;
   struct proc *p = curproc;
   /* for now, just include this to keep the compiler from complaining about
      an unused variable */
#if OPT_A2
   KASSERT(lk_process_table);
   KASSERT(process_table);
   lock_acquire(lk_process_table);
   // remove process status
   for (unsigned i = 0; i < array_num(process_table); i++) {
      struct process_status* ps = array_get(process_table, i);
      KASSERT(ps);
      // set self
      if (ps->pid == curproc->pid) {
         KASSERT(exitcode >= 0);
         ps->exitcode = _MKWAIT_EXIT(exitcode);
         ps->valid = false;
         // wake parents
         cv_broadcast(ps->cv_waitpid, lk_process_table);
      }
      // remove all children
      else if (ps->parent == curproc->pid) {
         if (ps->valid) {
            kprintf("%d exits, its child %d is still valid\n", curproc->pid, ps->pid);
            debug_status(get_process_status(ps->pid));
            die();
         };
         process_status_destroy(ps);
         array_remove(process_table, i);
         i--;
      }
   }
   lock_release(lk_process_table);
#else
   (void)exitcode;
#endif

   DEBUG(DB_SYSCALL, "Syscall: _exit(%d)\n", exitcode);

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
   return (0);
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
   KASSERT(lk_process_table);
   KASSERT(process_table);
   lock_acquire(lk_process_table);
   struct process_status* st = get_process_status(pid);
   // process exists
   if (st) {
      // if process is a child
      KASSERT(st->parent);
      KASSERT(curproc->pid);
      if (st->parent == curproc->pid) {
         // if process is running
         if (st->valid) {
            // kprintf("%d asleep\n", curproc->pid);
            cv_wait(st->cv_waitpid, lk_process_table);
            // kprintf("%d woke\n", curproc->pid);
         }
         // child exits
         if (WEXITSTATUS(st->exitcode) < 0) {
            kprintf("%d exits with %d\n", st->pid, st->exitcode);
            debug_status(get_process_status(st->pid));
            die();
         };
         err = copyout(&(st->exitcode), status, sizeof(int));
         if (err) {return err;}
         *retval = st->pid;
         lock_release(lk_process_table);
         return 0;
      }
      // debug();
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
      return (EINVAL);
   }
   /* for now, just pretend the exitstatus is 0 */
   exitstatus = 0;
   result = copyout((void *)&exitstatus, status, sizeof(int));
   if (result) {
      return (result);
   }
   *retval = pid;

   return (0);
#endif
}


#if OPT_A2
/* Fork related stuff */

int sys_fork(struct trapframe* tf, pid_t* rv) {
   KASSERT(lk_process_table);
   KASSERT(process_table);
   struct proc* proc = proc_create_runprogram(curproc->p_name);
   if (! proc) {
      return -1;
   }
   if (as_copy(curproc_getas(), &(proc->p_addrspace))) {
      proc_destroy(proc);
      return -1;
   }
   
   lock_acquire(lk_process_table);
   spinlock_acquire(&curproc->p_lock);
   struct trapframe* childtf = kmalloc(sizeof(struct trapframe));
   *childtf = *tf;
   if (thread_fork(proc->p_name, proc, &enter_forked_process, childtf, 1)) {
      proc_destroy(proc);
      kfree(childtf);
      spinlock_release(&curproc->p_lock);
      return -1;
   };
   KASSERT(proc->pid >= PID_MIN);
   *rv = proc->pid;
   spinlock_release(&curproc->p_lock);
   
   spinlock_acquire(&proc->p_lock);
   const pid_t newpid = proc->pid;
   spinlock_release(&proc->p_lock);
   
   spinlock_acquire(&curproc->p_lock);
   const pid_t curpid = curproc->pid;
   spinlock_release(&curproc->p_lock);
   
   save_process_status(newpid, curpid);
   lock_release(lk_process_table);
   
   return 0;
}

#endif
