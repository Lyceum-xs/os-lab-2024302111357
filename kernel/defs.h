#ifndef DEFS_H
#define DEFS_H
#include "types.h"

struct proc;
struct context;
void printf(const char *, ...);
void panic(const char *) __attribute__((noreturn));
void consoleinit(void);
void consputc(int);
void consoleintr(int);
int consoleread(uint64, int);
int consolewrite(uint64, int);
void uartinit(void);
void uartintr(void);
int uartgetc(void);
void uartpoll(void);
void uartputc_sync(int);
void plicinit(void);
void plicinithart(void);
int devintr(void);
void timerintr(void);
int timer_should_yield(void);
void trapinit(void);
void kerneltrap(void);
uint64 prepare_return(struct proc *);
uint64 usertrap(void);
void procinit(void);
void userinit(void);
void scheduler(void) __attribute__((noreturn));
struct proc *myproc(void);
void yield(void);
void sleep(void *);
void wakeup(void *);
void procdump(void);
void swtch(struct context *, struct context *);
int kfork(void);
void kexit(int) __attribute__((noreturn));
int kwait(uint64);
int kexec(const char *);
uint64 growproc(int);
int argint(int, int *);
int argaddr(int, uint64 *);
int argstr(int, char *, int);
void syscall(void);
void *memset(void *, int, uint);
void *memcpy(void *, const void *, uint);
int memcmp(const void *, const void *, uint);
uint strlen(const char *);
int strcmp(const char *, const char *);
#endif
