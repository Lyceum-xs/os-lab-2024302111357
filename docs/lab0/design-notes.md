# lab0 设计笔记

## 项目范围

lab0 不修改内核代码，目标是用一次 `echo hi` 贯通用户态、内核态、陷入、中断、进程和文件描述符。图纸以课程提供的 xv6 RISC-V 参考树为源码依据；课程版命名 `kfork/kexec/kexit/kwait` 与上游 `fork/exec/exit/wait` 对应。

## 任务一：echo hi 控制流

1. Shell 在 `user/sh.c` 中通过 `read` 等待输入。
2. UART 接收字符触发外部中断，`uartintr` 读取字符并交给 `consoleintr`。
3. 控制台输入缓冲区积累命令；回车使等待中的 `consoleread` 返回，Shell 获得 `echo hi`。
4. Shell 调用 `fork`。父进程继续等待，子进程得到 `a0=0`，父进程得到子 PID。
5. 子进程调用 `exec`，内核释放旧地址空间并装入 `user/echo` 的 ELF 镜像。
6. `echo` 在 U 态调用 `write`，通过 `ecall` 进入 S 态，`sys_write` 最终调用 `consolewrite` 和 UART 输出。
7. `echo` 调用 `exit` 进入僵尸状态并唤醒等待中的 Shell；Shell 的 `wait` 回收子进程后重新打印 `$ `。

每次 `ecall` 返回前都必须将 `sepc` 前移 4 字节；外部中断不前移 `sepc`，因为它不是当前用户指令的同步陷入。

## 任务二：exec 后数据结构

关键截面是子进程 `exec` 完成、第一条用户指令尚未执行时。进程表记录 Shell、echo 和 init 的 PID、父子关系、状态以及当前进程的 trapframe。echo 的地址空间从低地址开始放置 text、rodata、data/bss、用户栈和堆；高地址固定映射 TRAPFRAME 与 TRAMPOLINE。页表项需标注 U、R、W、X 权限，trampoline 在用户页表和内核页表中使用相同虚拟地址。

echo 的标准输入、标准输出和标准错误通过进程 `ofile` 数组指向 file 结构，再通过 inode/设备号连接到控制台设备。`write(1, ...)` 因此不会写普通文件，而是走控制台设备的 `consolewrite`。

## 任务三：时钟中断与上下文切换

用户进程运行时，时钟中断以 `scause=0x8000000000000005` 进入 `uservec/usertrap`。trampoline 保存用户通用寄存器到 trapframe，C 代码判断中断来源并更新 tick；达到让出条件后，进程状态改为 RUNNABLE，通过 `swtch` 保存进程 context 并切到 CPU 专属 scheduler context。调度器选择 RUNNABLE 进程后反向 `swtch`，恢复进程内核栈和 context，`prepare_return/usertrapret` 设置用户态返回条件，`userret` 恢复寄存器，`sret` 回到原用户态 PC。

trapframe 保存用户态完整现场；context 只保存软件上下文切换所需的 callee-saved 寄存器和返回地址。调度器不能运行在即将被回收的进程内核栈上，否则退出进程释放或复用该栈时会破坏调度器自身的执行现场。

## 关键机制批注

### trampoline 双重映射

陷入入口开始执行时仍使用用户页表，返回用户态前又必须切换回用户页表。如果 trampoline 在两张页表中的虚拟地址不同，切换 `satp` 后下一条取指会使用错误的虚拟地址，导致 instruction access fault。因此两端必须在同一虚拟地址映射同一段 trampoline 代码。

### fork 双重返回值

父进程的系统调用返回值是子 PID；子进程创建时把自己的 trapframe `a0` 预置为 0。返回值按照 RISC-V ABI 放在 `a0`，所以一次内核 fork 操作可以让两个后续执行流观察到不同结果。

### 独立调度器栈

进程退出后其内核栈可能立即被回收或重新分配。若 scheduler 继续使用该栈，栈上的返回地址和局部变量会失效；使用每个 CPU 独立的 scheduler context 可以让调度器脱离被切换或被销毁的进程。

## 验证证据

执行 QEMU 时可附加 `-d int -D int.log`，按 `async:0`、`cause` 和 `epc` 切片。系统调用应出现 cause 8，UART/磁盘设备使用外部中断 cause 9，时钟中断应出现 supervisor timer interrupt。内核地址可用 `riscv64-unknown-elf-addr2line -e kernel/kernel` 翻译；用户地址可先用 `objdump` 定位 `ecall` 桩。
