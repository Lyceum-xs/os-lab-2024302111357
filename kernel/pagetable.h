#ifndef PAGETABLE_H
#define PAGETABLE_H

#include "riscv.h"

extern pagetable_t kernel_pagetable;
void kinit(void);
void *kalloc(void);
void kfree(void *);
uint kfreepages(void);
uint ktotalpages(void);
pte_t *walk(pagetable_t, uint64, int);
int mappages(pagetable_t, uint64, uint64, uint64, int);
void uvmunmap(pagetable_t, uint64, uint64, int);
void kvminit(void);
void kvminithart(void);
pagetable_t uvmcreate(uint64);
void uvmfree(pagetable_t);
int uvmcopy(pagetable_t, pagetable_t, uint64);
int uvmgrow(pagetable_t, uint64, uint64);
uint64 userpa(pagetable_t, uint64, int);
int usercheck(pagetable_t, uint64, uint64, int);
int copyin(pagetable_t, void *, uint64, uint64);
int copyout(pagetable_t, uint64, const void *, uint64);
int copyinstr(pagetable_t, char *, uint64, uint64);
void dump_pagetable(pagetable_t);
void vmselftest(void);

#endif
