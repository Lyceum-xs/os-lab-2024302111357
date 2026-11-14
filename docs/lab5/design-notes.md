# lab5 设计笔记

日期：2026-11-14；手册 V3、增量包 v2-20261109。学号 2024302111357：引用方案 1，全局数组；单进程 COW 预算 253；kill 语义 1，完成当前 COW 后再退出。基准为单核，双核未实现。

## 起点与目标

| 模块 | lab4 结束时 | 本次目标 |
| --- | --- | --- |
| kalloc/kfree | 位图记录空闲、直接回收 | 按 PA 索引的 uint16 refcnt，锁内增减，归零才填 0x5a |
| uvmcopy | 深拷贝每个用户页 | 共享物理页；仅原可写页标记 COW；只读代码仍只读 |
| usertrap | store fault 直接退出 | 合法 COW 分离；非法地址/只读代码退出；epc 不前移 |
| usercheck/copyout | 只认可 PTE_W | 先完整验证地址区间，再处理 COW 后写入 |
| kexec/sys_exec | 忽略 argv | 先复制旧空间参数，再构造新栈，成功返回 argc |
| 观测 | free 页数 | pmc(0) 真拷贝总数，pmc(1) refcnt>1 的页数 |

## 同一可写页的三张状态图

```text
1. fork 完成
父 PTE [W=0 COW=1] ----+---> PA0 [ref=2, old data]
子 PTE [W=0 COW=1] ----+

2. 子首次写（实际复制一次）
父 PTE [W=0 COW=1] --------> PA0 [ref=1, old data]
子 PTE [W=1 COW=0] --------> PA1 [ref=1, child data]

3. 父随后写（原地恢复，不分配第三页）
父 PTE [W=1 COW=0] --------> PA0 [ref=1, parent data]
子 PTE [W=1 COW=0] --------> PA1 [ref=1, child data]
```

## 计数与方案分析

| 场景 | 操作与代码位置 | 漏做的后果 |
| --- | --- | --- |
| 分配 | kalloc 在 memlock 内将 refcnt[PA索引] 置 1 | 首次释放下溢 |
| fork 共享 | uvmcopy 的子映射成功后 kref_inc | 任一方退出即可提前回收另一方的数据 |
| COW 分离 | cow_alloc 先复制和换映射，再 kfree(old) | 旧计数虚高造成泄漏；提前减则可能读到毒值 |
| 解除映射/退出 | uvmunmap/free_tree 调 kfree，归零才入池 | 泄漏或双重释放 |

128 MiB / 4096 = 32768 项，uint16 数组占 65536 字节，索引 `(pa-KERNBASE)/PGSIZE`。位图与 refcnt 共用 memlock，增减 O(1)，共享页数在 1→2 与 2→1 边界更新。kinit 直接建立空闲池，所有空闲页 ref=0，无须用运行期 kfree 初始化。

方案 0 的 RSW 仅 bit8/bit9；COW 占一位后无法容纳任意多共享者，且 kfree(pa) 不知道应改哪张页表。方案 2 每次回收扫描 N 个进程的三级页表，单次 O(N×页表项数)，批量释放 M 页可达 O(M×N×页表项数)，并会在 wait_lock/p.lock 下引入跨进程锁倒序。它也统计不到页表页等非用户叶子对象。两者都需要全局计数作为权威；本学号方案 1 直接使用数组。

现有 mappages 用 bit8 标记事务新映射，与 PTE_COW 冲突。本次将短暂事务标记移至 bit9，映射提交/回滚时清除；方案 1 不使用 RSW1。fork 先完成全部子映射，全部成功后才清父写位，故失败无需修补父 PTE；子进程未变 RUNNABLE 前不会执行，失败映射由调用者 freeproc 释放。

## 缺页、copyout 和终止

cow_alloc 顺序验证 VA<MAXVA、有效叶 PTE、U 位、W/COW 状态。只读代码不带 COW，必须拒绝。ref=1 仅恢复 W 并清 COW，不增加计数；ref>1 分配新页、复制、换映射、递减旧引用并 sfence.vma。修改父 PTE 后也刷新 TLB，store fault 返回重试原指令。

内核 copyout 走 PA 恒等映射，不会触发用户态缺页，因此 usercheck 的写校验和 copyout 都须显式处理 COW。先查完整范围（包括后续页权限），再分离，保证越界不会造成部分用户数据写入。OOM 可以留下已经合法分离但内容未变的页，不留下泄漏或错误引用。

每进程 cow_copies 初始为 0，fork 子计数从 0 开始；exec 保留同一进程的累计数。只在真实拷贝成功后增加；恰到 253 时告警一次并继续复制，保证 bench 不因配额终止。pmc(0) 全局累加真实拷贝；pmc(1) 是共享物理页数，不是引用总和。

本学号 kill 语义 1：store fault 先完成合法分离，然后统一检查 killed 退出。trapframe 每进程独占、U=0；trampoline 是共享内核代码、U=0，二者不参加 uvmcopy 的用户页共享。若共享 trapframe，uservec 的 sd 寄存器保存会覆盖另一进程现场；若将它改成只读，uservec 首批 sd 即 S 态 store fault，入口现场尚未完成，无法正常返回。

单核不在内核态抢占，COW 事务不能被另一用户进程并发执行。为了不把锁内页引用管理误解成已支持多核，双核仍不声明通过：未来还需 cow_lock 串行化“读 ref→分配→换页→减旧 ref”、跨核 TLB 同步、进程表锁与运行核管理。否则两核同时读 ref=2 会多复制一次。引用增减本身若不锁，两个 CPU 都读 2 再各写 1，最后一次释放永远到不了 0；共享递增丢失则会提前释放造成 UAF。

## exec 参数与兼容边界

sys_exec 用一页临时内核缓冲存参数字符串，小型指针数组留内核栈。逐项 copyin 用户指针、copyinstr 字符串；MAXARG=32，预留 argv[argc]=0，最多 31 个字符串；累计字符串超过一页或新栈装不下则返回 -1。任意非法地址/整数溢出都先拒绝，旧空间保持完整。

kexec 在临时页表中逐项从栈顶向下放字符串，再放指针数组，最终 sp=argv 且 16 字节对齐。tf.a1=sp；成功返回 argc，syscall 赋给 tf.a0。初始 sh 直接启动时单独设置 a0。兼容旧自测 exec(name, NULL) 为 argc=0，非零坏指针则拒绝。

预置 sh 只按空格/Tab 分词，但旧预置 gets 保留行末 CR/LF。用独立 sh_input.c 包装 gets，仅在编译 sh.o 时映射为 sh_gets；去掉行结束符，保留 exec 参数中的合法换行字节。预置 sh.c、ulib.c 保持原文。cowpair.c 有未使用的 static 变量，仅该目标关闭 unused-variable 警告，课程测试正文不改。

## 实现前确定的自测

1. cowtest：同一页重复 fork/交替写；子进程与父数据隔离，回收后 pmc(1)=0。
2. 多个并发子进程写入，退出码与引用守恒；非法代码页写入必须杀子进程而非内核。
3. 父子共享 255 个页，子逐页写入跨过 253 阈值，告警一次后仍完成。
4. exec：合法多参数、空字符串、最大参数数、过长参数、非法 argv 地址/元素、跨页参数；失败保留旧映像。
5. 内核 COW 自测：分配/共享/逐次释放、fork 部分映射 OOM、COW 分离 OOM、ref=1 原地恢复、copyout 跨页拒绝、kill 语义分支。
6. 课程 zerocopy/cowpair/echo，历史 lab1、lab2、lab3、lab4 全套回归；干净归档重建。

已知预期：zerocopy 通常只复制用户栈 1 次；cowpair 的栈页与堆页各复制 1 次，合计精确 2 次。实际记录见后续 verification.md，不以设计预期冒充测试结果。
