# exec 真实用户栈与 COW 对照

2026-11-14 补充。参考 xv6 图仍见 [data-structures.svg](data-structures.svg)；本页记录课程自研内核 lab5 的实际结果，不把参考 xv6 的 ELF、文件表或 guard 实现套到课程内核上。

## echo hi：exec 提交后、返回用户态前

复现：`make test-cow-kernel`。`kernel/exec.c` 仅在 `LAB5_SELFTEST` 下，从已经构造好的新用户页表用 copyin 读回栈内存。Python 测试逐项检查指针、哨兵、填充和字符串内容。

```text
EXECSTACK argc=2 sp=0x4fc0 argv0=0x4ff0 argv1=0x4fe0 sentinel=0x0
EXECWORD va=0x4fc0 data=0x4ff0
EXECWORD va=0x4fc8 data=0x4fe0
EXECWORD va=0x4fd0 data=0x0
EXECWORD va=0x4fd8 data=0x0
EXECWORD va=0x4fe0 data=0x6968
EXECWORD va=0x4fe8 data=0x0
EXECWORD va=0x4ff0 data=0x6f686365
EXECWORD va=0x4ff8 data=0x0
hi
T5-3 PASS: argc=2 argv[0]=echo aligned
```

| 用户虚拟地址（高到低） | 内容及关系 | 权限 |
| --- | --- | --- |
| TRAMPOLINE = 0x3ffffff000 | uservec/userret，与内核页表映射相同 PA | R-X，U=0 |
| TRAPFRAME = 0x3fffffe000 | 本进程独占的寄存器保存页 | RW-，U=0 |
| 0x5000 起 | exec 后初始堆顶；sbrk 向高地址增长 | 尚未分配 |
| 0x4000–0x4fff | 一页用户栈，从 0x5000 向低地址压入参数 | RW-，U=1 |
| 0x2000–0x3fff | 两页 guard | 无映射 |
| 0x1000–0x1fff | echo 数据/BSS | RW-，U=1 |
| 0x0000–0x0fff | echo 代码和只读数据 | R-X，U=1 |

栈顶 64 字节的解读如下（RISC-V 小端）：

| 地址范围 | 64 位值或字节 | 含义 |
| --- | --- | --- |
| 0x4ff0–0x4fff | `65 63 68 6f 00`，其后填充 0 | `echo` 字符串，argv[0] 指向这里 |
| 0x4fe0–0x4fef | `68 69 00`，其后填充 0 | `hi` 字符串，argv[1] 指向这里 |
| 0x4fd8–0x4fdf | `0x0000000000000000` | 对齐填充 |
| 0x4fd0–0x4fd7 | `0x0000000000000000` | argv[2] 的 NULL 哨兵 |
| 0x4fc8–0x4fcf | `0x0000000000004fe0` | argv[1] |
| 0x4fc0–0x4fc7 | `0x0000000000004ff0` | argv[0]，也是 sp 和 a1 |

`sp=0x4fc0`、`a1=0x4fc0` 均 16 字节对齐。kexec 返回 2，经 sys_exec 返回后由 syscall 写入 a0，用户 main 因此收到 argc=2。argc 不额外存放在栈顶。

手册 V3 的文字要求按 i=0..argc-1 压字符串，示意图却把两段字符串的位置互换。本实现遵循循环要求，先放 echo，再放 hi；指针顺序仍是 argv[0]=echo、argv[1]=hi，符合 ABI，以上记录来自真实内存。

## fork、写入与回收：同一个可写页

```text
fork 前
  父 [W=1 COW=0] ----------------> PA0 [ref=1]

fork 后
  父 [W=0 COW=1] ---+
                    +------------> PA0 [ref=2]
  子 [W=0 COW=1] ---+

子首次写，scause=15：分配 PA1，复制，改子 PTE，旧页 ref--
  父 [W=0 COW=1] ----------------> PA0 [ref=1, 原内容]
  子 [W=1 COW=0] ----------------> PA1 [ref=1, 子内容]
  pmc(0) 增加 1；pmc(1) 对这一个页减少 1

父随后写，scause=15：独占页原地恢复，零分配、零复制
  父 [W=1 COW=0] ----------------> PA0 [ref=1, 父内容]
  子 [W=1 COW=0] ----------------> PA1 [ref=1, 子内容]

子 exit -> ZOMBIE -> 父 wait 回收
  PA1 ref=1 -> 0，填充 0x5a，归还空闲池
  PA0 仍 ref=1；父解除映射后再归零回收
```

在本内核中 exit 先留下僵尸进程，真正的地址空间释放发生在 wait 的 freeproc；成功 exec 也会释放旧地址空间。只读代码允许共享但不带 COW；trapframe 每进程独占，trampoline 不纳入用户页引用共享。

这些图说明单个数据页；课程 cowpair 的实测总数为 2，因为除数据页外还有用户栈的一次分离。zerocopy 的 fork 后立即 exec 只产生一次旧用户栈复制，堆页没有被提前深拷贝。
