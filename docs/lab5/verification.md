# lab5 验证记录

日期：2026-11-14；学号：2024302111357。依据学生版 V3 和 v2-20261109 增量包。

## 环境与提交范围

- GCC 16.2.0，QEMU 11.0.1，Python 3.14.6；内核 `-Wall -Werror`。
- QEMU virt、128 MiB、`-smp 1`、`-bios none`；不宣称支持 SMP。
- `LAB5_REFCNT_SCHEME=1`、`LAB5_COW_BUDGET=253`、`LAB5_KILL_SEMANTICS=1`；course_sid.h 与 lab4-submit 逐字节一致。
- `93985f4` 补齐前序材料，`be6182f` 合并课程包并标记 lab5-start；旧 lab0–lab4 标签不移动。
- 课程 syscall.h、user.h、usys.pl、sh.c 和三项测试正文与增量包逐字节一致。Shell 行末处理在独立包装文件中，仅 cowpair 编译目标豁免课程源码的 unused-variable 警告。

macOS 设置 `DEVELOPER_DIR=/Library/Developer/CommandLineTools`。每个测试打印日志目录，串口原文和 QEMU 中断日志保存在该目录。

## 课程验收与综合自测

普通构建 `make -j4` 后执行 `make test-lab5`，全部通过：

```text
T5-1 copies: 0 -> 1 (delta 1)
T5-1 PASS: fork-immediately-exec ~zero copy
T5-2 copies delta = 2 (expect 2)
T5-2 PASS: pair-write exact 2 copies, data isolated
hi
T5-3 PASS: argc=2 argv[0]=echo aligned
COWTEST alternating writes PASS
COWTEST concurrent children PASS shared=0
COWTEST readonly protection PASS
[cow] pid=27 reached LAB5_COW_BUDGET=253, downgrade: continue copying
COWTEST budget downgrade PASS pages=255
COWTEST argv child PASS argc=5
COWTEST argv child PASS argc=31
COWTEST exec arguments and rejection PASS
COWTEST all PASS shared=0
PASS page conservation free=32501
LAB5 all PASS
```

这次 zerocopy 只分离旧用户栈，未深拷贝将被 exec 丢弃的堆。cowpair 的 2 次分别对应栈页与数据页；脚本要求精确 2，不能仅看到课程程序的 PASS 就忽略 WARN。

cowtest 包括 8 轮父子交替写、8 个并发子进程与退出码去重、VA=0 只读代码写入被终止、255 个页跨过配额且只告警一次。exec 覆盖普通/空/跨页字符串、31 个参数、跨页 argv 指针表，以及非法 path、非法 argv/字符串地址、越界、未终止字符串、栈空间不足和过多参数；失败后原进程继续执行。另验证非法 pmc 选择器返回 -1。

`echo`（argc=1）、`echo alpha beta`（argc=3）、bench 和 hi 均通过。全部命令结束并回收子进程后空闲页恢复 32501。

## 内核边界与真实栈快照

`make test-cow-kernel` 临时启用 LAB5_SELFTEST，结束后恢复普通构建：

```text
COWKTEST allocation share release PASS
COWKTEST partial-fork rollback PASS
COWKTEST copyout OOM and exclusive recovery PASS
COWKTEST kill-semantics=1 all PASS
EXECSTACK argc=2 sp=0x4fc0 argv0=0x4ff0 argv1=0x4fe0 sentinel=0x0
PASS actual argv pointers, sentinel, padding and strings
PASS page conservation free=32436
LAB5 all PASS
```

自测直接调用正式实现，检查 ref=1→3→2→1→0、未归零时内容保持；只留 2 个空闲页使 fork 部分页表创建失败，释放子页表后父权限和引用恢复。再耗尽全部空闲页，验证 COW OOM 不改原 PTE、引用或计数，而 ref=1 仍能原地恢复。copyout 跨入未映射页必须先拒绝，不产生前半段写入；合法 copyout 分离后父内容不变。

kill 分支用合成的已标记 killed 进程调用与 usertrap 相同的 cowfault 函数，确认语义 1 仍完成当前复制；真实进程 kill 唤醒和退出由 lab4 schededge 回归覆盖。这不是多核终止竞态测试。自测构建包含额外保留数组，因此 32436 不应与普通构建的 32501 直接比较；两种构建均比较各自运行前后页数。

栈快照从新页表实际 copyin 读回 64 字节并断言，详见 [exec 真实栈与 COW 对照](../lab0/02-exec数据结构快照.md)。

## 前序回归

| 命令 | 本轮结果 |
| --- | --- |
| `make test-lab1` | printf 整数边界、字符串、十六进制、百分号及孤立百分号通过；Banner 原字节和五种畸形边界通过 |
| `make test-lab2` | 空读阻塞、64 字节容量、三轮回绕、非法 fd/长度通过 |
| `make test` | 代码/数据权限、两页 guard、跨页访问、两次 memcap、vmstress、Shell/spin 并发全部通过 |
| `make test-lab4` | bench、抢占、schededge、官方 stride、自编固定份额/改票、旧异常用例全部通过 |
| `make test-uart` | BUFSTORM lines=4，EXPECT 失败 0 条 |
| `make test-kernel` | LAB3_SELFTEST 分配器、映射、扩展失败回滚通过 |
| `make test-sched-kernel` | 默认分发 100:200:400，票数覆盖、继承、唤醒、pass 归一化通过 |

两次 memcap 均分配 2026 块后返回 OOM；vmstress 容量为 132829184 字节。lab3 与 lab4 用户态测试前后空闲页均为 32501。lab4 本轮 bench 并发子进程 8 个，avg_fork_wait=2688 个 time CSR 计数；stride_ratio=76:224，fixed_share=30:86，changed_share=90:30，均落入测试断言范围。这些采样数不是固定性能承诺。

宿主 printf 测试使用 UBSan 和逐字节断言；本机 ASan 初始化超时，未宣称 ASan 通过。三组会清理重建的内核测试依次执行，不与其他构建并行。

## 干净归档复验

使用 `git write-tree` 导出暂存内容，以 `git archive` 解压至全新目录。导出目录不含 .git、kernel/kernel、user-flat 和 fs.img，确认 doc/lab0 符号链接可正确访问快照材料后执行：

```bash
make -j4
make test-lab1 test-lab2 test test-lab4 test-lab5 test-uart
```

全部通过。lab5 再次报告 zerocopy=1、cowpair=2、COWTEST all PASS shared=0；普通构建空闲页仍为 32501，UART EXPECT 失败 0 条。最终提交相对该次测试导出内容只补充本段记录，源码与测试没有再改动。提交 ZIP 从 lab5-submit 生成，不包含编译产物、运行日志和本机缓存；旧实验 ZIP 与标签保持不变。
