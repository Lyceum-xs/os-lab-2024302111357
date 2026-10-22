# lab3 验证记录

日期：2026-10-22。学号：2024302111357。依据实验说明书 V3.1 和 lab3-v2-20260929 增量包。

## 环境与命令

使用 RISC-V GCC 16.2.0、QEMU 11.0.1、Python 3.14.6。QEMU 配置为 `virt`、128 MiB RAM、1 个 hart、`-bios none`。个人参数为降序分配、两页保护区、扣除 13 页配额。

```bash
make clean
make -j4
python3 check_expect.py 2024302111357
make test
make test-kernel
make test-uart
make dump
```

本机使用 `DEVELOPER_DIR=/Library/Developer/CommandLineTools` 运行 Make/Git，避开尚未接受许可的完整 Xcode 安装。课程 lab0 preflight 可以运行；有“未找到 GDB”和 PMP monitor 探测未确认的提示，运行测试已覆盖 S/U 态切换和 PMP 配置后的用户访问，未使用 GDB 单步调试。

## 功能测试

| 项目 | 观察结果 |
| --- | --- |
| 编译 | `-Wall -Werror` 构建通过，最终链接无警告 |
| lab1 Banner | check_expect 格式检查通过；QEMU 原始输出前缀与 expect_banner.txt 逐字节一致 |
| 页表观测 | 自动 dump、Ctrl-P 重放、L2/L1/LEAF 格式和四位权限检查通过 |
| lab2 基础调用 | hi 输出 PID；badecall 输出 `TEST-1 PASS` |
| 官方 badaccess | `TEST-3a PASS`、`TEST-3b PASS`，故障子进程退出状态为 -1 |
| vmguard | 两个保护页分别读写，四个子进程均被终止，父进程继续运行 |
| vmcopy | 跨页 write/read/exec 字符串/wait 状态拷贝通过；代码页、内核专用页及无效地址被拒绝 |
| 官方 memcap | 连续两轮均在 2028 个 64 KiB 块后返回 OOM；已有数据保持完整 |
| vmstress | fork 物理隔离、失败 exec、堆收缩及重新增长清零、OOM 后 fork/exec 回滚通过 |
| 页面守恒 | 功能与压力测试前后空闲页均为 32530 |
| 并发 | vmconcurrent 中 spin 持续输出时，Shell 仍可执行 hi 和 badecall |
| 课程 UART 回归 | inject_uart.py 执行 storm.txt，输出 `BUFSTORM lines=4 bytes=256`，EXPECT 全部通过 |

压力测试输出：

```text
TEST-4 PASS: OOM returned error after 2028 chunks; existing alloc intact; kernel alive
TEST-4 PASS: OOM returned error after 2028 chunks; existing alloc intact; kernel alive
VMSTRESS PASS fork exec shrink rollback capacity=132952064
pid=1 name=sh sz=0x5000 free=32530
```

上述数值对应当前编译配置和嵌入的测试程序；增加程序、改变优化选项或启用内核自测会改变镜像占用，不应将 2028、32530 硬编码为通用配额。

文件比对确认 lab3 的 trampoline.S、riscv.h、badaccess.c、memcap.c 与增量包完全一致，lab2 其余赠送代码也保留原内容。inject_uart.py 沿用 lab2-submit 的版本；相对课程原脚本只把启动提示符判断从 `$ ` 扩展为同时接受 `sh> `，测试注入及 EXPECT 逻辑未变。lab2 测试说明保存在 support/README-tests.md。

## 页表与陷入证据

首次 sh 装配完成后的实际输出：

```text
DUMP-PAGETABLE begin
L2 va=0x0 pa=0x87fa2000 perm=----
L1 va=0x0 pa=0x87fa1000 perm=----
LEAF va=0x0 pa=0x87fa3000 perm=r-xu
LEAF va=0x1000 pa=0x87fa0000 perm=rw-u
LEAF va=0x4000 pa=0x87f9f000 perm=rw-u
L2 va=0x3fc0000000 pa=0x87fa5000 perm=----
L1 va=0x3fffe00000 pa=0x87fa4000 perm=----
LEAF va=0x3fffffe000 pa=0x87faa000 perm=rw--
LEAF va=0x3ffffff000 pa=0x80002000 perm=r-x-
DUMP-PAGETABLE end
```

`0x2000`、`0x3000` 没有叶项，正好对应两页保护区；`0x4000` 是用户栈。TRAPFRAME 和 TRAMPOLINE 均未设置 U 位。与 lab0 各对象的关系列在设计笔记中。

`nm` 检查：trampoline/uservec 为 `0x80002000`，userret 为 `0x8000209c`，kernel_text_end 为 `0x80003000`，kernel_end 为 `0x80090000`。跳板正确进入可执行段，不是地址 0。

非法写代码页与越过堆边界的日志节选：

```text
USER-FAULT pid=5 cause=0xf epc=0xc va=0x0
TEST-3a PASS: wild write killed (status=-1)
USER-FAULT pid=6 cause=0xd epc=0x48 va=0x7000
TEST-3b PASS: beyond-sbrk access killed (status=-1)
```

QEMU `-d int` 日志同时记录到 cause 5 的 `s_timer`；配合 spin 与 Shell 同时运行的输出，验证用户态定时抢占路径。完整串口和陷入日志由测试脚本写入临时目录，不加入提交包。

## 内核失败路径

`make test-kernel` 使用编译开关 `LAB3_SELFTEST`，默认运行版本不执行这些测试。自测完成后出现 `KTEST all PASS`，随后恢复普通内核构建。

- 分配地址按最高空闲页优先、4 KiB 对齐、分配清零、释放后重用，计数守恒。
- 分配到配额耗尽，返回空指针；全部释放后恢复初始空闲数量。
- 重复映射返回 -1，已有映射不被覆盖。
- walk 创建中间表失败时回收部分结构。
- 只留一页空闲内存，构造跨 1 GiB 边界的映射失败；旧叶项、原有空子树和计数均保留。
- 跨 2 MiB 边界的堆增长失败后恢复原映射与空闲计数。
- copyout 目标第二页无权限时，第一部分也不写入。

## 范围

本次验证仅针对单核课程环境。LAZY 请求按 EAGER 处理；内嵌程序没有文件系统、通用 ELF 运行时装载和完整 argv 支持。多核同步、按需分页及后续实验能力不在本次实现范围。现场验收中的权限讲解和口头问答仍需本人完成。
