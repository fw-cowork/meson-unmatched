# Unmatched M-mode CSR 检查

该程序由 M-mode U-Boot 的 `go` 命令调用，通过 Unmatched 控制台 UART0 打印
`mhartid`、`mstatus` 和 `mtvec`，然后返回 U-Boot。

构建产物集中在
`builddir/baremetal/unmatched-mmode-check/`，TFTP 使用其中的
`unmatched-mmode-check.bin`。

```text
=> tftpboot 0x84000000 unmatched-mmode-check.bin
=> go 0x84000000
unmatched-mmode-check: probing M-mode CSRs
  mhartid = 0x0000000000000001
  mstatus = 0x8000000a00007800
  mtvec   = 0x00000000fff68600
M-mode CSR access: PASS
## Application terminated, rc = 0x0
```

程序复用 U-Boot 已经配置为 115200 8-N-1 的 UART0，不改 UART divisor、PRCI 或引脚
复用。FU740 没有 UART3：它只有 UART0 (`0x10010000`) 和 UART1 (`0x10011000`)，
Unmatched 的 `stdout-path` 使用 UART0。

在默认 S-mode U-Boot 下运行时，第一条 M-mode CSR 读取会触发非法指令异常；这种情况
需要通过串口观察 trap，并可能需要复位开发板。
