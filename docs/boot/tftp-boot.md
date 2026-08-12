# HiFive Unmatched U-Boot TFTP 启动

本文档说明如何在 HiFive Unmatched 上使用静态 IPv4 配置，通过 U-Boot TFTP：

- 启动包含 Linux 内核和设备树的单个 FIT；
- 更新 SD 卡中的 SPL、OpenSBI 和 U-Boot。

## 当前配置

| 项目 | 值 |
|---|---|
| TFTP 服务器地址 | `192.168.1.23` |
| Unmatched 板卡地址 | `192.168.1.24` |
| 子网掩码 | `255.255.255.0` |
| Linux TFTP 文件 | `unmatched-fit.itb` |
| 固件更新文件 | `unmatched-firmware.itb` |
| 两个 FIT 的配置名 | `unmatched` |
| Linux rootfs | SD 卡 `/dev/mmcblk0p4` |
| 串口 | `ttySIF0,115200` |

该流程不依赖 DHCP。板卡和 TFTP 服务器必须能在 `192.168.1.0/24` 网络中直接
通信。

## 启动链与文件来源

```text
FU740 Boot ROM
  -> SD loader1: u-boot-spl.bin
  -> U-Boot SPL 初始化 DDR
  -> SD loader2: u-boot.itb
       -> OpenSBI fw_dynamic（M-mode，打印启动信息）
       -> U-Boot proper（S-mode）
            -> TFTP: unmatched-fit.itb
                 -> Image.gz
                 -> hifive-unmatched-a00.dtb
            -> bootm ${fit_addr_r}#unmatched
                 -> Linux
                      -> SD p4: BusyBox rootfs
```

各产物的来源如下：

| 产物 | 加载位置 | 作用 |
|---|---|---|
| `u-boot-spl.bin` | SD 卡 loader1 raw 分区 | 初始化 DDR，加载下一阶段 |
| `u-boot.itb` | SD 卡 loader2 raw 分区 | 包含 OpenSBI、U-Boot proper 和启动 DTB |
| `unmatched-fit.itb` | TFTP 服务器 | 包含 Linux 内核、Unmatched DTB 和 SHA-256 hash |
| `unmatched-firmware.itb` | TFTP 服务器 | 包含 SPL、`u-boot.itb`、更新脚本和 SHA-256 hash |
| `rootfs.ext4` | SD 卡第 4 分区 | Linux 根文件系统 |

`run tftp_boot` 只加载 Linux FIT，不改变卡上固件。更新 SPL、OpenSBI 和 U-Boot
应使用本文后面的 `run tftp_update_firmware`；OpenSBI 已包含在 `u-boot.itb` 中，
不需要单独写入 `fw_dynamic.bin`。

## OpenSBI 打印配置

Unmatched U-Boot 配置包含：

```text
CONFIG_SPL_OPENSBI_SCRATCH_OPTIONS=0x2
```

SPL 将该值写入传给 OpenSBI 的 `fw_dynamic_info.options`：

- 不设置 `SBI_SCRATCH_NO_BOOT_PRINTS` (`0x1`)，所以显示 OpenSBI 启动信息；
- 设置 `SBI_SCRATCH_DEBUG_PRINTS` (`0x2`)，所以允许 `sbi_dprintf()` 输出。

这不同于 OpenSBI 构建参数 `DEBUG=1`。`DEBUG=1` 主要用于关闭编译优化，不是
启动日志开关。

## 1. 构建镜像

完整构建 SD 卡镜像：

```bash
cd /home/adrian/devel/riscv/meson-unmatched
./build.sh sd-image
```

主要输出为：

```text
deploy/u-boot-spl.bin
deploy/u-boot.itb
deploy/unmatched-firmware.itb
deploy/Image.gz
deploy/hifive-unmatched-a00.dtb
deploy/unmatched-fit.itb
deploy/rootfs.ext4
deploy/unmatched-lite.img
```

## 2. 准备 TFTP 服务器

在 `192.168.1.23` 上将两个 FIT 复制到 TFTP root。以下路径假设 TFTP root 为
`/srv/tftp`：

```bash
sudo cp deploy/unmatched-fit.itb /srv/tftp/
sudo cp deploy/unmatched-firmware.itb /srv/tftp/
sudo chmod 0644 /srv/tftp/unmatched-fit.itb
sudo chmod 0644 /srv/tftp/unmatched-firmware.itb
```

服务器应允许 UDP 69 端口和 TFTP 数据连接。可在服务器本机先确认文件存在：

```bash
ls -lh /srv/tftp/unmatched-fit.itb /srv/tftp/unmatched-firmware.itb
```

## 3. 首次写入或恢复 SD 卡

空白 SD 卡、GPT 布局不匹配或板卡已无法进入 U-Boot 命令行时，应使用读卡器写入
本次生成的完整镜像：

```bash
sudo dd if=deploy/unmatched-lite.img of=/dev/sdX bs=4M status=progress conv=fsync
```

`/dev/sdX` 必须替换为目标 SD 卡的整盘设备，而不是分区。执行前应使用 `lsblk`
核对设备，错误的目标会覆盖其他磁盘。

## 4. 检查 U-Boot 环境

启动板卡并在倒计时期间中断自动启动。新默认环境包含：

```text
serverip=192.168.1.23
ipaddr=192.168.1.24
netmask=255.255.255.0
```

检查实际值：

```text
=> printenv serverip ipaddr netmask
```

如果 SPI Flash 中保存过旧环境，持久化值可能覆盖新版默认值。只恢复本流程使用
的 Linux 启动和固件更新变量：

```text
=> env default fit_addr_r ipaddr serverip netmask tftp_boot tftp_fit tftp_bootargs firmware_addr_r firmware_fit tftp_update_firmware
```

需要跨重启保存这些值时执行：

```text
=> saveenv
```

## 5. 可选检查网络

排障时可以先验证服务器连通性：

```text
=> ping ${serverip}
```

预期输出包含：

```text
host 192.168.1.23 is alive
```

`ping` 是一个独立的 U-Boot 网络会话，会额外初始化一次 MAC/PHY。正常启动不必
先执行它；如果 ping 失败，检查网线、交换机端口、服务器防火墙、双方 IP 和
子网掩码，不要继续排查 TFTP 文件。

## 6. 启动 Linux

执行默认环境中的启动命令：

```text
=> run tftp_boot
```

`tftp_boot` 的实际逻辑为：

```text
if tftpboot ${fit_addr_r} ${tftp_fit}; then
    setenv bootargs ${tftp_bootargs}
    bootm ${fit_addr_r}#unmatched
fi
```

默认变量展开后等价于：

```text
tftpboot 0x84000000 unmatched-fit.itb
setenv bootargs root=/dev/mmcblk0p4 rw rootwait console=ttySIF0,115200 earlycon=sbi loglevel=8
bootm 0x84000000#unmatched
```

这只执行一次 `tftpboot`，所以不会在下载内核和 DTB 之间再次初始化 PHY。FIT 中
的 kernel 和 fdt 都带 SHA-256 hash；`bootm` 校验通过后把压缩内核加载到
`0x80200000` 并使用 FIT 内的 Unmatched DTB。

## 7. 通过 TFTP 更新 SPL、OpenSBI 和 U-Boot

先在板端确认更新目标是包含本仓库 GPT 布局的 SD 卡：

```text
=> mmc list
=> part list mmc 0
```

默认目标固定为 `mmc 0`，第 1 分区应为 `SPL`，第 2 分区应为 `U-Boot`。确认
TFTP 服务器上的 `unmatched-firmware.itb` 来自本次构建，然后执行：

```text
=> run tftp_update_firmware
```

该命令只建立一次 TFTP 会话，随后 FIT 内的更新脚本会按以下顺序执行：

1. 校验更新脚本、SPL 和 `u-boot.itb` 的 SHA-256；
2. 检查 `mmc 0` 第 1、2 分区的 GPT type GUID；
3. 从实际 GPT 读取两个分区的起始扇区和容量，并拒绝超大镜像；
4. 先写第 2 分区中的 `u-boot.itb`，回读并逐字节比较；
5. 再写第 1 分区中的 SPL，回读并逐字节比较。

这里的 SHA-256 用于发现传输或存储损坏，并不认证发布者；FIT 当前没有签名。
固件更新应只在可信的直连网络中进行，并确认 TFTP 文件来源。

成功时最后显示：

```text
Firmware update verified - reset the board
```

看到这行以后再执行 `reset`。`u-boot.itb` 同时包含 OpenSBI 和 U-Boot proper，
所以第 4 步已经完成 OpenSBI 更新。该流程不修改 Linux、rootfs、GPT 和 SPI
Flash 中保存的 U-Boot environment。

### 旧版 U-Boot 首次升级

如果当前 U-Boot 还没有 `tftp_update_firmware`，可以直接执行固件 FIT 自带的同一
份更新脚本：

```text
=> setenv serverip 192.168.1.23
=> setenv ipaddr 192.168.1.24
=> setenv netmask 255.255.255.0
=> tftpboot 0x84000000 unmatched-firmware.itb
=> setenv verify yes
=> source 0x84000000#unmatched
```

重启进入新版 U-Boot 后，可恢复并保存以后使用的简化命令：

```text
=> env default firmware_addr_r firmware_fit tftp_update_firmware
=> saveenv
```

### 更新风险与恢复

该板卡的 loader1/loader2 布局不是 A/B 更新，写卡过程不具备断电原子性。网络
下载、FIT hash 或分区检查失败都发生在写入之前；但在 `mmc write` 期间断电或
复位仍可能导致 SD 卡无法启动。更新时应保证供电稳定，并提前保留可用的
`deploy/unmatched-lite.img` 和读卡器。

无法进入 U-Boot 时，最直接的恢复方法是按“首次写入或恢复 SD 卡”重新烧写完整
镜像。若要保留后面的 boot/rootfs 分区，可在主机严格确认 `/dev/sdX` 是目标 SD
卡后，只恢复两个 loader 分区：

```bash
sudo dd if=deploy/u-boot.itb of=/dev/sdX bs=512 seek=2082 conv=notrunc,fsync
sudo dd if=deploy/u-boot-spl.bin of=/dev/sdX bs=512 seek=34 conv=notrunc,fsync
```

这些扇区号只适用于本仓库生成的 GPT 布局；布局不确定时应恢复完整镜像。

## 手工调试

需要定位失败阶段时，逐条执行：

```text
=> setenv serverip 192.168.1.23
=> setenv ipaddr 192.168.1.24
=> setenv netmask 255.255.255.0
=> setenv fit_addr_r 0x84000000
=> tftpboot ${fit_addr_r} unmatched-fit.itb
=> setenv bootargs root=/dev/mmcblk0p4 rw rootwait console=ttySIF0,115200 earlycon=sbi loglevel=8
=> bootm ${fit_addr_r}#unmatched
```

TFTP 成功时，U-Boot 会显示下载文件名、服务器地址、加载地址和传输字节数。

## 后续更新内核

OpenSBI、SPL 和 U-Boot 不变时，无需重新烧写 SD 卡。重新构建 Linux 后，只需
更新 TFTP 服务器上的 FIT：

```bash
./build.sh linux
sudo cp deploy/unmatched-fit.itb /srv/tftp/
```

然后重启板卡并再次执行：

```text
=> run tftp_boot
```

只修改 OpenSBI 或 U-Boot 时执行 `./build.sh u-boot`，它会同时刷新
`deploy/unmatched-firmware.itb`。更新 TFTP 服务器上的该文件后执行：

```text
=> run tftp_update_firmware
```

## 常见问题

### `ping` 报服务器不可达

检查 `printenv ipaddr serverip netmask`、物理链路和服务器防火墙。当前双方位于
同一 `/24` 子网，不需要 `gatewayip`。

### TFTP 报 `File not found`

检查 TFTP root 中的文件名大小写。默认名称必须是：

```text
unmatched-fit.itb
unmatched-firmware.itb
```

### Linux 找不到 rootfs

默认 `bootargs` 使用 `/dev/mmcblk0p4`。确认使用的是本仓库生成的 SD 镜像，且
第 4 分区存在。若 rootfs 位于其他设备，启动前覆盖 `tftp_bootargs`。

### 看不到 OpenSBI 打印

只执行 `run tftp_boot` 不会更新 OpenSBI。使用新生成的
`unmatched-firmware.itb` 执行 `run tftp_update_firmware`，或恢复完整 SD 镜像。

### 固件更新在写入前停止

检查 FIT hash 报错、`part type` 不匹配或镜像大于 loader 分区时，命令会在写卡
前停止。不要绕过这些检查；确认 TFTP 文件来自同一次构建，并确认目标确实是
本仓库生成的 `mmc 0` SD 卡。
