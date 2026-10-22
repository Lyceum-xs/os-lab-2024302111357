# lab3 设计笔记

学号：2024302111357。对应实验说明书 V3.1 和 2026-09-29 v2 增量包。

## 设计与约束

- `LAB3_ALLOC_ORDER=1`：每次取当前空闲页中物理地址最高的一页。
- `LAB3_GUARD_PAGES=2`：数据段之后留两页未映射区域，再放一页用户栈。
- `LAB3_MEMCAP_DELTA=13`：分配区从 `PGROUNDUP(kernel_end)` 开始，到 `PHYSTOP - 13 * PGSIZE` 为止。内核页表仍恒等映射到 PHYSTOP。
- 单核运行。内核处理系统调用时关闭 S 态中断；阻塞通过保存内核 context 切回调度器，在调度器中开启中断。缓冲区与页分配器不会在同一核上被并发修改。此设计不宣称支持双核。
- 使用 Sstc 的 `time/stimecmp`，M 态开启 STCE、ADUE 和计时器访问权限。内核映射 UART 和 PLIC，不依赖 CLINT MMIO。
- 恢复课程原始 memlayout.h 和 kernel.ld，删除 lab2 的固定物理地址镜像交换。内核及用户派生链接脚本由构建程序生成，原始预置文件保持完整。

## 分配与回滚不变式

空闲页用位图表示，分配优先扫描高地址，释放为 O(1)。最高非空位图字索引避免从头重复扫描；和有序链表相比，它占用少量元数据，但批量回收不需要 O(n²) 插入。分配出的页全部清零，释放前填充垃圾字节，用于暴露悬挂引用。

128 MiB RAM 对应 32768 个页框，位图占 4096 字节并计入内核镜像。空闲链表可复用空闲页存储节点，额外元数据较少，但要持续满足最高地址优先时，有序插入的单次释放最坏为 O(n)。位图的释放只置一位并更新计数，分配最坏扫描位图，适合 memcap 的批量回收。清零还防止新进程读到上一所有者的数据；垃圾填充只用于调试，不代替访问权限检查。

`walk` 返回叶 PTE 的地址；创建中间表失败时，撤销本次创建的中间表。重复映射返回 -1，不覆盖旧映射。`mappages` 失败会撤销此次已安装的叶项，回收由此次创建、已为空的中间表；调用方提供的物理页仍归调用方所有。解除映射先清除 PTE，再刷新 TLB，最后释放物理页，避免旧翻译访问已经复用的页。

`sbrk` 增长失败恢复原边界、释放此次用户数据页和中间表；收缩不能穿过初始堆底。两种策略在本实验均即时分配。增量包中有一处写“返回新边界”，但预置用户库和 memcap 把返回值作为新分配区域的起点使用，因此遵循 sbrk 原有 ABI：返回旧边界，n=0 返回当前边界，失败返回 -1。

## 地址空间

用户低地址从 0 开始：代码和只读数据 `r-xu`，页对齐后的数据/BSS `rw-u`，两页未映射保护区，一页 `rw-u` 用户栈，其后是向上增长的堆。TRAPFRAME 为 `rw--`，TRAMPOLINE 为 `r-x-`，二者都不允许 U 态访问。

构建脚本从用户 ELF 提取入口、可写区起点与内存末尾，扩展内嵌程序表。派生用户链接脚本将可写区放到新页，避免代码和数据同页。BSS 由分配器清零，不依赖平铺二进制包含 BSS。

内核使用恒等映射的每进程独立物理栈，不采用 KSTACK 高地址别名；调度器保留独立引导栈。与 lab0 图中“每进程内核栈”的归属一致，但地址布局不同。派生内核链接脚本标出代码边界，使代码只读可执行，其余内存可读写。

页表切换与 PTE 拆除后执行 `sfence.vma`；进入用户页表必须经同址 trampoline。`usertrap` 首先切换 stvec 到 kernelvec，返回前再切回 uservec。A/D 位由硬件维护，不写入 dump 的四位权限字符串。

`satp = (8UL << 60) | (root_pa >> 12)`，ASID 为 0。激活内核页表前后均刷新 TLB；trampoline 在切换 satp 前后刷新，解除映射先使 PTE 无效并刷新，再回收页面。否则旧 TLB 项可能继续访问已归其他进程所有的物理页。多核需要通知使用该地址空间的其他核心执行 TLB shootdown，并等待确认后再复用页面；本实现只在 hart 0 运行。

保护页完全不安装 PTE，也不占用数据页。用户读、写、取指缺页分别产生 cause 13、15、12，`usertrap` 的非系统调用、非设备中断分支打印故障地址并执行 `kexit(-1)`；父进程通过 wait 获取状态。内核自身异常才进入 kerneltrap 的 panic 路径。

### 与 lab0 快照的对应

lab0 图纸依据课程参考 xv6，包含本阶段尚未实现的文件对象。下表按本次启动的 `sh` 实际布局对照；用户程序大小变化时，数据末尾、保护区与栈的位置随之变化。

| lab0 中的对象 | lab3 对应位置 | 权限或归属 |
| --- | --- | --- |
| 进程表与 PID、父子关系 | `proc_table`、`parent`、`state` | 内核内存，用户页表不可达 |
| text / rodata | 用户 VA `[0x0, 0x1000)` | `r-xu`，禁止写入 |
| data / BSS | 用户 VA `[0x1000, 0x2000)` | `rw-u`，不可执行 |
| 栈下保护页 | 用户 VA `[0x2000, 0x4000)` | 两页均未映射，dump 不出现 LEAF |
| 用户栈 | 用户 VA `[0x4000, 0x5000)` | `rw-u`，向低地址增长 |
| 堆 | 从 `0x5000` 向高地址增长 | 初始为空，sbrk 分配 `rw-u` 页 |
| trapframe | 用户 VA `0x3fffffe000` | `rw--`，每进程独立物理页 |
| trampoline | VA `0x3ffffff000` | `r-x-`，用户与内核页表同址共享 |
| 进程内核栈 | `kstacks[NPROC][8192]` | 内核恒等映射，不使用 KSTACK 高地址别名 |
| 调度器栈 | 引导栈 `stack0[8192]` | 与所有进程栈分离 |
| file / inode / ofile | 本阶段没有此层结构 | fd 0 读控制台，fd 1/2 写控制台 |

## 进程和用户指针

proc 保存 pagetable、独立 trapframe 页、sz、heap_base、内核栈和 context。fork 逐页深拷贝普通用户页，跳过保护区；trapframe 单独分配，trampoline 共享。exec 先在临时页表装配完整镜像，成功才替换旧空间。退出保留状态供 wait 回收，回收在另一条内核执行流上进行，不释放正在使用的栈。

copyin/copyout/copyinstr 按页检查有效位、U 位及读写权限，通过恒等映射访问对应物理页。涉及 read、write、exec 名称、wait 状态指针；不能把用户 VA 直接强转为内核指针。copyout 先验证整段目的区，防止跨页错误造成部分写入。现场可通过 Ctrl-P 重放当前进程或 Shell 页表，不增加课程系统调用号。

逐字节查页表会重复完成同一页的三级遍历；按字宽直接拷贝则容易跨越页尾或忽略物理页不连续。本实现先逐页验证完整缓冲区，再以页内剩余长度为上限批量 memcpy，每个片段独立翻译 PA。字符串以页为单位翻译，在页内逐字节查找 NUL。内核关闭中断且进程无共享地址空间线程，验证与拷贝之间不会被另一执行流修改页表。

`allocproc` 创建独立 trapframe 页和页表；`kfork` 深拷贝普通用户页及寄存器值，保护区保持为空，trapframe 的内核字段在返回前重新填写。trampoline 是共享只读代码，不复制。`kexec` 在新页表中借用同一 trapframe 页，失败只回收新页表和新数据页；成功后更新 pagetable/sz/heap_base/epc/sp，再释放旧用户空间。`kwait` 调用 `freeproc` 回收僵尸进程的用户页、各级页表及 trapframe；静态内核栈留给该进程槽下一次使用。

配额耗尽时 `kalloc` 返回 0，walk 返回空 PTE 指针，mappages/uvmgrow/uvmcopy/kexec 返回 -1。各层只释放自己拥有的资源；fork 的最终清理由 freeproc 负责，exec 失败保留旧进程，sbrk 只在增长成功后更新 sz。没有把可预期的 OOM 转成 panic。

### 用户指针审查

检查命令：

```bash
rg -n 'argaddr|argstr|copyin|copyout|copyinstr|usercheck|memcpy|\(.*\*\)' \
  kernel/syscall.c kernel/sysproc.c kernel/console.c kernel/exec.c kernel/proc.c
```

逐个追踪寄存器参数的使用位置，结果如下：

| 调用路径 | 用户提供的地址 | 处理方式 |
| --- | --- | --- |
| sys_read → consoleread | a1 输入目标缓冲区 | usercheck 验证 W/U；copyout 后才消费字符 |
| sys_write → consolewrite | a1 输出源缓冲区 | usercheck 验证 R/U；copyin 到内核小缓冲区 |
| sys_exec → argstr | a0 程序名称 | copyinstr 到内核数组，再交给 kexec |
| sys_wait → kwait | a0 退出状态目标地址 | 非零地址验证 W/U，再 copyout；0 表示不接收状态 |
| sys_fork → uvmcopy | 父进程地址空间 | 从已验证 PTE 取 PA，逐页深拷贝 |

检索到的 `exec.c` memcpy 源是内核内嵌程序表中的镜像地址，proc 中的 memcpy 用于内核进程名，均非用户 VA。trampoline 间接调用使用固定内核常量地址。syscall 分发仍对未知或尚未实现的号码返回 -1；没有遗留直接解引用用户指针的系统调用。

## 实现前确定的测试

1. `vmguard`：子进程分别读写用户栈下方两页保护区，wait 应得到非零退出状态；父进程和 Shell 继续运行。
2. `vmcopy`：通过 sbrk 得到两页，在页边界两侧构造字符串调用 write；跨页读取输入、跨页 exec 名称、跨页 wait 状态应正确。向代码页、trapframe、超大地址及未映射区写入应返回 -1。
3. 内核自测：分配顺序、清零、计数守恒；只剩少量空闲页时让映射跨越 L1/L2 边界，检查失败后旧映射和空闲页计数不变。
4. `vmstress`：fork 隔离、失败 exec 保留旧镜像、sbrk 收缩/再增长；重复 OOM 和失败 fork/exec 后检查释放后的可用内存恢复。
5. 官方 badaccess、memcap，以及 lab2 badecall、bufstorm、inject_uart 和 lab1 Banner 回归。

实际结果见 [verification.md](verification.md)。
