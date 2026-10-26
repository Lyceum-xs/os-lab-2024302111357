#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "riscv.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"

struct context {
  uint64 ra, sp;
  uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

struct cpu {
  struct context context;
  struct proc *proc;
  int noff;
  int intena;
  uint rr_idx;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct trapframe {
  uint64 kernel_satp;
  uint64 kernel_sp;
  uint64 kernel_trap;
  uint64 epc;
  uint64 kernel_hartid;
  uint64 ra;
  uint64 sp;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
};

struct proc {
  struct spinlock lock;
  int pid;
  enum procstate state;
  struct proc *parent;
  int exit_status;
  int killed;
  int cpu_ticks;
  int last_slice_ticks;
  int tickets;
  int tickets_set;
  uint64 stride;
  uint64 pass;
  uint64 kstack_top;
  struct trapframe *tf;
  pagetable_t pagetable;
  uint64 sz;
  uint64 heap_base;
  void *chan;
  struct context context;
  char name[16];
};

extern struct proc proc_table[NPROC];
extern struct cpu cpus[NCPU];

#endif
