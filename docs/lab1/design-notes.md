# lab1 设计与补充自测

补齐日期：2026-11-14。本文和边界用例是对既有提交的补充，不声称是在原编码前完成；原 `lab1-submit` 保留。

## 启动和不变式

Boot ROM 从 0x1000 转到链接入口 0x80000000。entry.S 关闭中断，仅 hart 0 继续，按 `LAB1_STACK_KB=8` 建立 16 字节对齐栈。start 设置 MPP=S、mepc=main、satp=0、异常委托和 PMP 后执行 mret。缺少 PMP 时，S 态取指会失败；不能把无输出直接归为串口问题。

printf 只负责格式解析，consputc/uartputc_sync 轮询 UART LSR 的 0x20 位后写 THR。整数负值通过 `-(value+1)+1` 转换到无符号幅值，避免最小负数直接取反溢出。当前内核 `%x` 的参数约定为 64 位无符号数，使用者须传 uint64 或 unsigned long long。

学号协议为 1：原串 `oslab sid=2024302111357 mod97=0x32\n` 的每个字节后都追加点号，包括换行。预期文件的最终字节必须是点号，不能额外附加换行。

## 自测

`make test-lab1` 编译真正的 kernel/printf.c，将 consputc 替换为内存收集器，用逐项断言与 UndefinedBehaviorSanitizer 验证：0、-1、INT_MIN、INT_MAX、空字符串、普通字符串、%%、0x0、小写十六进制、最大 64 位无符号数和末尾孤立 `%`。后者在终止符后放 X 哨兵，必须停止解析，不能读出哨兵。补测发现原实现会越过终止符，已在格式解析处加入停止条件。

协议测试逐字节匹配个人 Banner，并拒绝缺末点、缺中间点、多换行、错误大小写、前导零五类输入。课程 check_expect.py 保留原样；它是形状检查，实际启动字节由 QEMU 回归另行检查。

`make test` 与 `make test-lab4` 同时比较真实 QEMU 启动输出，避免宿主格式化测试替代硬件启动验收。
