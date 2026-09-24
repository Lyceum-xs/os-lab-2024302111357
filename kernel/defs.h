#include "types.h"

struct proc;

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
void prepare_return(struct proc *);
uint64 usertrap(void);
void userret(uint64) __attribute__((noreturn));

void procinit(void);
void userinit(void);
void scheduler(void) __attribute__((noreturn));
struct proc *myproc(void);
void schedule_after_trap(int);
int kfork(void);
void kexit(int) __attribute__((noreturn));
int kwait(uint64);
int kexec(const char *);
void wake_parent(struct proc *);

int argint(int, int *);
int argaddr(int, uint64 *);
int argstr(int, char *, int);

void syscall(void);
int copyin(void *, uint64, int);
int copyout(uint64, const void *, int);
int copyinstr(char *, uint64, int);

void *memset(void *, int, uint);
void *memcpy(void *, const void *, uint);
int memcmp(const void *, const void *, uint);
uint strlen(const char *);
int strcmp(const char *, const char *);
