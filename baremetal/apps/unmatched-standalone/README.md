# Unmatched standalone runtime 检查

该程序验证 U-Boot `go` payload 已切换到自己的 16 KiB 栈，并由入口汇编清零 BSS。
检查完成后 hart 永久停留在 payload 中，不会返回 U-Boot；必须复位开发板才能恢复。

构建：

```bash
./build.sh unmatched-standalone-artifacts
```

通过 TFTP 运行：

```text
=> tftpboot 0x84000000 unmatched-standalone.bin
=> go 0x84000000
unmatched-standalone: private runtime
  sp = 0x000000008400ffXX
private stack: PASS
BSS clear: PASS
payload owns this hart; reset the board to recover
```

最后一行输出后不会出现 U-Boot 的 `Application terminated`，也不会重新出现命令提示符。
串口仍可显示 payload 已写入 UART FIFO 的内容，但 U-Boot 命令循环已经暂停。
