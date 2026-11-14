# 前序实验材料补齐记录

日期：2026-11-14。基于 lab4-submit 补齐 lab0–lab2 材料；这是事后补交，不把新增设计和测试伪装成原提交前已存在。

| 原缺项 | 补充内容 | 验证 |
| --- | --- | --- |
| lab0 图内批注、栈和锁 | 三张 SVG 重新整理，红字标明修正 | 浏览器逐张渲染检查 |
| lab0 实测证据 | evidence.txt、support/capture_lab0.py | 参考 xv6 的 echo hi 和三类硬件陷入 |
| 手册 doc/lab0 路径 | doc/lab0 链接至 docs/lab0 | Git 跟踪兼容链接 |
| lab1 设计与边界测试 | docs/lab1/design-notes.md、tests/printf_test.c | make test-lab1 通过 |
| lab2 两张图及设计取舍 | docs/lab2/ 下两张 SVG 和 design-notes.md | 对照源码及历史版本 |
| lab2 个人异常测试 | tests/ioedge.c、support/test_lab2.py | 空读、容量、回绕、非法参数通过 |
| lab0 与 lab5 的实现对照 | 02-exec数据结构快照.md | 真实 64 字节参数栈读回、COW 到 wait 回收的状态演进 |

补测同时修复 printf 对孤立 `%` 的越界解析。ASan 在本机启动超时，因此宿主用例采用 UBSan、逐字节断言和终止符后哨兵；不声称 ASan 已通过。

旧 lab0-submit 至 lab4-submit 保留。补充材料随本次补交提交和最终 lab5-submit 归档。原历史 ZIP 仍代表当时版本，不替换成新内容而沿用旧标签。

lab3、lab4 已有设计与验证材料继续保留。本次在 lab5 实现上重新执行两者的用户态、内核态测试以及课程 UART 回归，全部通过；本轮具体数值记录在 [lab5 验证](lab5/verification.md)。
