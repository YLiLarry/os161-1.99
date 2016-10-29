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
#include "debug.h"

#if OPT_A2
/* process related stuff */

void debug() {
   KASSERT(lk_process_table);
   KASSERT(process_table);
   kprintf("newtable: %d", array_num(process_table));
   for (unsigned i = 0; i < array_num(process_table); i++) {
      struct process_status* a = array_get(process_table, i);
      struct process_status* p = get_process_status(a->parent);
      kprintf("(%d,%d) ", a->pid, a->parent);
      KASSERT(!p || a->parent == p->pid);
   }
   kprintf("\n");
}

void debug_status(struct process_status* ps) {
   kprintf("process_status: pid %d, parent %d, valid %d, exitcode %d\n", ps->pid, ps->parent, ps->valid, ps->exitcode);
}

void die() {
   panic("die\n");
}

#endif
