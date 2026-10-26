# OS 实验（lab0-lab4）

学号：2024302111357

本项目是在 RISC-V QEMU 环境下完成的操作系统实验。当前版本启用 SV39 分页，为每个进程建立独立地址空间，支持 Shell、时钟抢占、步长调度、串口输入输出和物理内存管理。

## 实验内容

- lab0：阅读 xv6 代码，整理 `echo hi` 的执行过程，完成控制流图、数据结构图和时钟中断流程图。
- lab1：完成内核启动、M 态到 S 态切换、UART 轮询输出和基础 `printf`，实现学号 Banner 输出。
- lab2：完成 trampoline 和 trap 处理、系统调用、UART 输入中断、输入缓冲、进程切换以及内嵌用户程序装载。
- lab3：实现物理页分配器、SV39 三级页表、段权限与保护页、受控用户内存拷贝、即时分配的 `sbrk`，以及 `fork` 深拷贝和 `exec` 地址空间替换。
- lab4：引入每进程锁、CPU 调度上下文和标准 `sleep(chan, lk)`，实现 `pause`、`kill`、`schedstat`、`time_rdtime`、`setickets`，按个人参数 `LAB4_POLICY_ID=7` 实现步长调度。

lab4 默认票数为 `hi=1`、`spin=2`、`sh=4`，其他程序为 1。`setickets(n)` 可覆盖默认值，`fork` 继承票数设置，`exec` 保留显式设置。一个时间片包含两次用户态时钟中断；内核可以响应中断，但不在内核态抢占。

## 目录说明

```text
kernel/       内核启动、陷阱、系统调用、进程、页表和串口代码
user/         Shell 及内嵌用户程序
tests/        系统调用、输入缓冲、内存与调度测试
support/      构建脚本、自动化测试和课程标准测试
docs/lab0/    lab0 图纸和设计笔记
docs/lab3/    lab3 设计笔记、验证记录及接口规范
docs/lab4/    lab4 设计笔记、验证记录及接口规范
```

## 编译和运行

需要 RISC-V 裸机工具链（`riscv64-unknown-elf-gcc`、`ld`、`objcopy`、`nm`）、支持 Sstc 的 `qemu-system-riscv64`、Make、Python 3 和 Perl。编译并启动：

```bash
make clean
make -j4
make qemu
```

macOS 若因 Xcode 许可报错，而 Command Line Tools 已安装，可先设置：

```bash
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
```

启动时输出学号 Banner 和首个进程的页表，随后进入 `sh>`。每行输入一个程序名，例如：

```text
hi
spin
badecall
bufstorm
badaccess
memcap
bench
alt
stride_test
stride_cases
schededge
```

`spin` 持续运行；`vmconcurrent` 启动后台 `spin` 和新的 Shell，可同时观察调度与交互。`alt` 的两个子进程交错输出 A/B 后退出。按 Ctrl-P 重放当前进程（空闲时为初始 Shell）的页表，并显示进程状态、票数、CPU tick、时间片计数和 pass；按 Ctrl-A 后再按 X 退出 QEMU。

## 测试

检查 Banner 格式、lab3 回归与 lab4 功能：

```bash
python3 check_expect.py 2024302111357
make test
make test-lab4
```

`make test` 检查 Banner 原始字节、页表格式与权限、非法访存终止、跨页拷贝、两次内存耗尽、失败路径回滚、空闲页守恒以及 Shell 与 spin 的并发执行。串口和中断日志保存在命令输出的临时目录。

`make test-lab4` 检查官方 bench、A/B 交错输出、休眠和终止边界、官方 stride_test、固定份额与动态改票两个用例，以及 Banner、非法系统调用、非法访存和空闲页守恒。官方 lottery_test 也参与编译，但它适用于策略 3–5，不作为本学号策略的验收测试。

可分别运行策略、休眠、内核边界、课程串口回归和页表观测：

```bash
make test-policy
make test-sleep
make test-sched-kernel
make test-kernel
make test-uart
make dump
```

`test-sched-kernel` 临时启用 `LAB4_SELFTEST`，验证三个名称的默认票数、调度次数 1:2:4、大票数、fork 继承、唤醒和 pass 归一化。`test-kernel` 临时启用 `LAB3_SELFTEST`，验证分配器和映射失败回滚。这两个目标成功后会恢复普通构建；不要与其他构建目标并行执行，若中途失败可用 `make clean && make` 恢复。

`test-uart` 调用课程的 `inject_uart.py`，沿用 lab2 已提交的 `sh>` 提示符兼容调整；缺少 `fs.img` 时创建空白占位镜像，以满足该脚本的 QEMU 参数。本实验不使用文件系统，镜像和测试日志不提交到 Git。

实现与验证记录：[lab3 设计](docs/lab3/design-notes.md)、[lab3 验证](docs/lab3/verification.md)、[lab4 设计](docs/lab4/design-notes.md)、[lab4 验证](docs/lab4/verification.md)。

## 版本标签

```text
lab0-submit
lab1-start
lab1-submit
lab2-start
lab2-submit
lab3-start
lab3-submit
lab4-start
lab4-submit
```

各实验可从对应的 `labx-submit` 标签独立导出。lab4 归档命令：

```bash
git archive --format=zip -o 提交-lab4-2024302111357.zip lab4-submit
```

当前按单核、128 MiB RAM 的课程 QEMU 配置验收，未实现双核选做内容。用户程序仍作为平铺二进制嵌入内核，按虚拟地址 0 链接；页表装载时区分只读代码与可写数据。`SBRK_LAZY` 沿用 lab3 的即时分配行为，尚未实现按需分页、文件系统和后续实验功能。
