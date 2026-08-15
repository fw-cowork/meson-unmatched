# U-Boot M-mode 下通过 TFTP 运行 baremetal

## 目标和边界

这个实验只解决一件事：让 HiFive Unmatched 的 U-Boot proper 保持在 M-mode，使用
U-Boot 已有的网卡和 TFTP 命令下载 raw baremetal BIN，再由 `go` 调用并返回 U-Boot。

```text
ZSBL -> U-Boot SPL (M) -> U-Boot proper (M)
                              |
                              +-> tftpboot 0x84000000 <payload>.bin
                              +-> go 0x84000000 [args...]
                              +-> payload return -> U-Boot prompt
```

这条链路不启动 OpenSBI，也不用于启动 Linux。默认的
`ZSBL -> SPL -> OpenSBI -> U-Boot(S) -> Linux` 构建目标保持不变。
M-mode 实验变体同时关闭 EFI loader；它只保留命令行、网卡、TFTP 和 `go` 所需功能。

## 实现原理

`sifive_unmatched_defconfig` 默认构建 S-mode U-Boot。`u-boot-mmode` 在独立输出目录
中把 U-Boot proper 改为 `CONFIG_RISCV_MMODE=y`。该选择会关闭 SBI 和
`CONFIG_SPL_OPENSBI`，并启用 FU740 的 ACLINT 驱动，为 M-mode SMP/IPI 提供本地
CLINT 寄存器访问。

默认 RISC-V `u-boot.itb` 以 OpenSBI 为 `firmware`、U-Boot 为 `loadable`。
本项目补丁在 `CONFIG_SPL_OPENSBI=n` 时改成：

```dts
configurations {
    conf-1 {
        firmware = "uboot";
        fdt = "fdt-1";
    };
};
```

SPL 读取到 `os = "u-boot"` 后直接跳到 U-Boot proper，不执行 OpenSBI 的
`mret` 降权过程。RISC-V `go` 最终按普通 C ABI 调用下载地址，也不执行特权级
切换，因此 payload 继承 M-mode。

## 构建

先准备项目固定的 Freedom-U-SDK 工具链和 U-Boot 主机构建依赖，再执行：

```bash
./build.sh u-boot-mmode unmatched-mmode-check-artifacts
```

主要产物：

```text
out/u-boot-mmode/.config
deploy/mmode/u-boot-spl.bin
deploy/mmode/u-boot.itb
deploy/mmode/unmatched-mmode-firmware.itb
builddir/baremetal/unmatched-mmode-check/unmatched-mmode-check.bin
```

构建脚本会拒绝缺少 `CONFIG_RISCV_MMODE=y`、`CONFIG_RISCV_ACLINT=y`，或者仍然
启用 S-mode、SBI、SPL OpenSBI 的结果。

可以进一步做静态检查：

```bash
rg -n 'RISCV_(M|S)MODE|RISCV_ACLINT|SBI|SPL_OPENSBI|CMD_TFTPBOOT' \
  out/u-boot-mmode/.config
out/u-boot-mmode/tools/dumpimage -l deploy/mmode/u-boot.itb
```

预期配置包含 M-mode、ACLINT 和 TFTP，且 S-mode、SBI 和 SPL OpenSBI 功能开关
都不为 `y`；`CONFIG_SPL_OPENSBI_LOAD_ADDR` 只是未使用的 Kconfig 地址常量。FIT 中
只有 U-Boot 和设备树，不包含 `OpenSBI fw_dynamic Firmware`。

## 安装 M-mode 固件

固件更新会覆盖 SD 卡的 loader1 和 loader2 分区。第一次实验应使用可恢复的测试卡，
并保留默认 `deploy/unmatched-firmware.itb` 或完整 SD 镜像。

把 `deploy/mmode/unmatched-mmode-firmware.itb` 放到 TFTP 根目录。在当前正常的
S-mode U-Boot 中执行：

```text
=> setenv serverip 192.168.1.23
=> setenv ipaddr 192.168.1.24
=> tftpboot 0x84000000 unmatched-mmode-firmware.itb
=> setenv verify yes
=> source 0x84000000#unmatched
```

脚本会校验 FIT hash、GPT 分区类型和容量，写入后再读回比较。完成后断电重启。
串口日志应显示 SPL 加载的 OS 是 U-Boot，不应再出现 OpenSBI banner。

## TFTP 和 M-mode 验证

把 `unmatched-mmode-check.bin` 放到 TFTP 根目录：

```text
=> ping ${serverip}
=> tftpboot 0x84000000 unmatched-mmode-check.bin
=> go 0x84000000
unmatched-mmode-check: probing M-mode CSRs
  mhartid = 0x0000000000000001
  mstatus = 0x8000000a00007800
  mtvec   = 0x00000000fff68600
M-mode CSR access: PASS
## Application terminated, rc = 0x0
```

CSR 数值会随运行状态和 U-Boot 重定位地址变化。这个 payload 通过 U-Boot 已初始化的
UART0 打印结果，只读 `mhartid`、`mstatus` 和 `mtvec`，不修改 CSR。三者都是 M-mode
CSR；在 S-mode 执行会在打印 probing 行之后触发非法指令异常。能够返回 `rc = 0x0`
同时证明：

1. M-mode U-Boot 的 MAC/PHY 和 TFTP 正常；
2. `go` 下载地址与 payload 链接地址都是 `0x84000000`；
3. payload 确实具有 M-mode CSR 访问权限；
4. payload 保存了 U-Boot 返回地址和栈，能够回到命令行。

验证后可以运行现有程序：

```text
=> tftpboot 0x84000000 unmatched-tests.bin
=> go 0x84000000 all

=> tftpboot 0x84000000 unmatched-led.bin
=> go 0x84000000 blink red
```

## 限制和恢复

- `tftpboot` 是 U-Boot 下载 payload；进入 payload 后，U-Boot 网络栈不再执行。
  baremetal 若要继续收发网络包，需要自己的驱动和协议栈。
- 不要从这个实验固件直接启动 Linux 或 EFI 程序。Linux 需要 S-mode 入口和 SBI
  服务，本变体不是 SBI firmware，并且没有启用 EFI loader。
- payload 若修改 `mtvec`、中断、PMP、缓存或时钟，返回前必须恢复；普通 `return`
  不会自动回滚硬件状态。
- M-mode 检查程序不要在默认 S-mode U-Boot 下执行，否则预期会进入非法指令异常。
- 启动失败时，把默认 `u-boot-spl.bin` 和 `u-boot.itb` 重新写入 loader1/loader2，
  或直接恢复默认 SD 镜像。
