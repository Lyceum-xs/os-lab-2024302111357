# lab2 设计与补充自测

补齐日期：2026-11-14。本文按历史 lab2-submit 解释原方案，再注明 lab3/lab4 的演进；新增用例归入当前补交版本，不改写原提交日期。

## 系统调用往返

见 [write 往返图](write-roundtrip.svg)。U 态将 fd/buf/n 放进 a0/a1/a2，a7 放调用号，执行 ecall。硬件设置 sepc/scause/SPP 并跳 stvec，不切栈也不保存通用寄存器。uservec 借 sscratch 定位 trapframe，按汇编偏移保存现场，取 kernel_sp/kernel_satp/kernel_trap，切换内核执行环境。C 侧用编译期断言检查 a0=112、t6=280、大小=288，避免 C 与汇编布局漂移。

usertrap 只在 ecall 时将 epc 加 4，系统调用返回值经 tf->a0 交回。argint 取寄存器整数，argaddr 提取地址值，argstr 必须校验并复制字符串。lab2 平坦地址版本使用受范围约束的地址，lab3 后使用 copyin/copyout/copyinstr 逐页翻译及校验。userret 恢复用户页表与寄存器，sret 返回用户栈上的下一条指令。异步中断不得 epc+=4。

## 输入中断

见 [UART 到 Shell 唤醒图](uart-wakeup.svg)。使能链为 UART IER → PLIC enable/priority → sie.SEIE → sstatus.SIE，设备中断由 devintr 分发，claim 后必须 complete。r/w/e 分别为消费、已提交、编辑下标；始终保持 r <= w <= e 且 e-r <= 64，下标用无符号差值和模容量处理回绕。

个人参数 `LAB2_BUF_SEMANTICS=1`，每收到一个字符便推进 w，read 读到当前可用部分即可返回；行缓冲模式则只在换行或满缓冲时推进 w。空读必须等待而不刷屏；零长度读立即返回。缓冲满时保留旧数据，丢弃新字符，不覆盖未读字符。

lab2 原方案用静态进程槽和保存的陷入现场，在 wait/read 处让出，不能把它解释为 lab4 的标准锁交接。现行版本在 cons.lock 下检查空缓冲，sleep 先取 p->lock 再释放 cons.lock，UART 写入后持 cons.lock 调用 wakeup，再由调度器恢复读取者，避免检查条件与休眠之间丢失通知。lab4 支持内核中断但不在内核态抢占。

## 装载和首个现场

无文件系统，程序来自构建时生成的 _uprog_table。lab2 使用内核镜像末尾之外的装载区并构造 epc/sp；lab3 改成每进程独立 SV39 页表，程序链接 VA=0，数据后两页保护区、一页用户栈，sp 满足 16 字节对齐。内核直接创建初始 sh，不另造一个空 init 程序。

## 自测与回归

`make test-lab2` 运行自写 ioedge：

1. 在空缓冲 read 前输出 ready，宿主等待 0.15 秒并确认读取没有提前结束，再发送 Q；程序核验至少跨过两个 tick，覆盖阻塞唤醒。
2. 连续三轮读取恰好 64 字节的模式数据，在目标两端放哨兵，检查容量边界、部分读取与环形回绕。宿主分批发送，以免 UART 硬件 FIFO 的独立容量干扰软件缓冲断言。
3. 检查非法 fd、负长度与零长度，随后重新运行 badecall，确认 Shell 仍能执行命令。

课程原 badecall、bufstorm 保留，`make test-uart` 运行课程串口集；`make test` 检查 Banner、跨页用户指针和 spin/sh 并发。上述新材料补足原设计与自测缺项，历史标签本身仍保持不变。
