# exec 与 COW 后续对照

2026-11-14 补充。参考 xv6 图见 data-structures.svg。课程内核在 lab3 已采用代码、可写数据、两页未映射 guard、一页用户栈与向上增长的堆。

lab5 的目标是在用户栈中放置参数字符串和以 NULL 结尾的 argv 指针数组；最终 sp 与 argv 首地址相等，按 16 字节对齐。a0 传 argc，a1 传 argv。完整实测布局将在 lab5 验证完成后记录于此。
