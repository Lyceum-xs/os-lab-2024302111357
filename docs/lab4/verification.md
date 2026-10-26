# lab4 验证记录

验证日期：2026-10-26。学号：2024302111357。

## 环境与范围

- 说明书：实验 4 学生版 V3，增量包 lab4-v2-20261009。
- 编译器：riscv64-unknown-elf-gcc 16.2.0，`-Wall -Werror`。
- 模拟器：QEMU 11.0.1，virt、128 MiB、`-smp 1`、`-bios none`。
- 策略：`LAB4_POLICY_ID=7`，步长调度；默认 hi/spin/sh 为 1/2/4 票。
- 一次 tick 为一次时钟中断，周期为 100000 个 time CSR 计数；每个时间片为 2 个用户态 tick。
- 双核为选做内容，本次未实现、未验收。

macOS 本机使用 `DEVELOPER_DIR=/Library/Developer/CommandLineTools` 调用 Make 和 Git。测试日志由脚本写入临时目录，同时记录串口和 QEMU `-d int` 输出。

## 机制与策略

普通构建执行 `make clean`、`make -j4`、`make test-lab4`，结果如下：

```text
PASS boot and Banner
PASS bench: PASS: bench
PASS timer preemption: AABBBAAAABBBBBBAAAAAABBBBBAAAABB
PASS schededge: PASS: schededge
PASS stride_test: PASS: stride
PASS stride_cases: PASS: stride_cases
PASS hi: hi: user program running, pid=124
PASS badecall: TEST-1 PASS: unknown syscalls all return -1
PASS lab3 badaccess regression
PASS page conservation (free=32522)
RESULT avg_fork_wait = 5973
RESULT concurrent_children = 8
RESULT stride_ratio = 74:214
RESULT fixed_share = 30:86
RESULT changed_share = 84:28
LAB4 all PASS
```

`avg_fork_wait` 单位为 time CSR 计数，不是 tick。包内实际 bench 源码定义 `ROUNDS=100`；说明书中的 16 轮描述与该版本源码不同，本次保持课程源码原样，实际执行 100 轮，并验证并发的 8 个子进程退出码齐全。

官方 stride_test 使用学号推导的 12、36 票（`5 + 2024302111357 % 15 = 12`）。六个采样窗口中双方都获得 CPU，累计比例 214/74 约为 2.89，落在 2.4–3.6 的判据内。自测固定 6:18 票得到 30:86；在运行中交换为 18:6 后得到 84:28，优势方向翻转。数值会随中断时刻变化，断言依据份额范围及每窗口无饥饿，不依赖固定输出数字。

alt 的 A/B 各 16 个字符，脚本验证字符总数与至少两次交替，防止仅凭 `alt done` 误判抢占成功。schededge 覆盖 pause 时长、无效票数、INT_MAX 票数、存活/已退出 PID、跨页与非法用户指针、退出码回写、kill 唤醒 pause 和控制台读取中的进程。

`make test-policy` 和 `make test-sleep` 可分别运行对应测试，并检查执行前后空闲页相等。完整测试也验证页数守恒。

## 内核自测与历史回归

`make test-sched-kernel` 在首次启动 Shell 前启用 `LAB4_SELFTEST`，运行实际选择函数 700 次，验证名称默认值及调度次数；检查显式票数在 exec 策略更新后保留、fork 继承设置且计数归零、唤醒与新建进程的 pass 下界、pass 归一化以及资源回收：

```text
SCHEDTEST named dispatches=100:200:400 PASS
SCHEDTEST overrides inheritance wakeup normalization PASS
SCHEDTEST all PASS
```

这个 100:200:400 是内核选择器的分发次数，用于确定性检查默认值；用户态 CPU tick 比例另由 stride_test、stride_cases 验证。

| 命令 | 结果 |
| --- | --- |
| `python3 check_expect.py 2024302111357` | 协议 1 Banner 形状通过；QEMU 启动测试另逐字节比对 |
| `make test-kernel` | 分配器、映射与扩展失败回滚内核自测通过，随后恢复普通构建 |
| `make test` | lab3 全部通过，包括保护页、跨页拷贝、两次 OOM、fork/exec/shrink 压力 |
| `make test-uart` | `BUFSTORM lines=4`，EXPECT 失败 0 条 |
| `make dump` | 页表格式、代码/数据/保护页权限和 Ctrl-P 重放通过 |

lab3 两次 memcap 均在 2028 个块后返回 OOM；压力测试报告容量 132915200 字节。整组测试前后空闲页均为 32522；后台 spin 与 Shell 并发时仍能运行 hi、badecall。

## 丢失唤醒复现与恢复

在独立临时副本中修改 sleep，正式工程未加入故障开关。先按手册把锁交接改为：

```c
release(lk);
for (volatile int i = 0; i < 1000000; i++)
  ;
acquire(&p->lock);
```

刚看到 `sh>` 就输入 bench 时，命令字符被回显，但 15 秒内无测试输出。UART 通知可以落在释放控制台锁后、设置 SLEEPING 前，此后无人再次唤醒读取者。等待 Shell 稳定 0.5 秒后再输入，bench 可以通过。因此不能把这次超时说成已经复现了父子进程退出竞态。

当前内核不在内核态抢占，空转期间时钟会推进，但其他用户进程不会自动运行。为明确复现父子退出的交错，在同一错误窗口、获取 p->lock 前增加仅针对 bench 的一次让出：

```c
if (chan == p && !strcmp(p->name, "bench"))
  yield();
```

这次 bench 停在第一轮 fork/wait。等待 3 秒后连续两次 Ctrl-P 快照均显示：

```text
PROC pid=1 name=sh state=SLEEPING tickets=4 cpu=0 slice=0 pass=0x1f400000000
PROC pid=2 name=bench state=SLEEPING tickets=1 cpu=0 slice=0 pass=0x4e200000000
PROC pid=3 name=bench state=ZOMBIE tickets=1 cpu=0 slice=0 pass=0x4e200000000
```

父进程先释放 wait_lock 并让出，子进程的退出通知发生在父进程正式休眠之前。子进程已为 ZOMBIE，父进程却仍在 SLEEPING；时钟持续工作也不能补回这条退出通知。这是额外安排执行顺序的故障注入，不是普通构建中的自然抢占。

移除延迟和显式 yield，恢复 `acquire(&p->lock); release(lk);` 后重新构建，同一副本的 bench 输出 `PASS: bench` 并返回 `sh>`。正确版本始终在释放条件锁之前持有进程锁；唤醒方获取该锁时已经能看到休眠状态。

## 提交检查

课程的 swtch.S、syscall.h、user.h、usys.pl、common.h、common.c、bench.c、lottery_test.c、stride_test.c 与增量包逐字节一致。`support/tests/README-lab3.md` 保存的是上一实验的测试说明，供 lab3 回归查阅；`README-tests.md` 更新为 lab4 版本。

保留 `lab4-start`、机制重构提交和最终策略提交，提交标签为 `lab4-submit`。归档仅包含受 Git 跟踪的源码、文档和测试；目标文件、内核镜像、用户程序构建目录及临时日志由 .gitignore 排除。

发布前从 Git 暂存树导出全新目录，执行 `make -j4`、`make test-lab4`、`make test`、`make test-uart`，全部通过。干净目录中的策略数据为 `stride_ratio=72:210`、`fixed_share=28:84`、`changed_share=82:28`，测试前后空闲页仍为 32522。完成后仅补充本段记录，源码未再改动。
