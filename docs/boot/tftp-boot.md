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

如果 SPI Flash 中保存过旧环境，持久化环境会作为一个整体覆盖新版 U-Boot 的
默认环境。因此，即使已经通过 `unmatched-firmware.itb` 更新了 SD 卡中的 SPL
和 U-Boot，`tftp_boot` 等新增变量也不会自动出现。命令行按 Tab 只能补全到
`type_guid_*`，或者 `run tftp_boot` 报变量未定义，都是这一情况。

只恢复本流程使用的 Linux 启动和固件更新变量：

```text
=> env default fit_addr_r ipaddr serverip netmask tftp_boot tftp_fit tftp_bootargs firmware_addr_r firmware_fit tftp_update_firmware
=> printenv tftp_boot tftp_fit fit_addr_r serverip ipaddr netmask
```

这是定向恢复，不会修改 EEPROM 读取的 `ethaddr`、`serial#`，也不会删除其他
自定义变量。确认输出正确后，将合并后的当前环境保存回 SPI Flash：

```text
=> saveenv
```

`unmatched-firmware.itb` 的更新脚本只写 SD 卡的 loader1 和 loader2 分区，不写
位于板载 SPI Flash `0x505000` 的 U-Boot environment，所以固件更新后只需执行
一次上述环境迁移。

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

### 更新原理

`unmatched-firmware.itb` 不是直接交给 Boot ROM 启动的 `u-boot.itb`，而是外层的
“更新容器”。它把两个待写入文件和一份 U-Boot 命令脚本打包到一个 FIT：

```text
unmatched-firmware.itb                 外层更新 FIT
  /images/spl                          写入 SD loader1
    data = u-boot-spl.bin（512 字节对齐）
    hash-1 = sha256
  /images/uboot                        写入 SD loader2
    data = u-boot.itb（512 字节对齐）
      OpenSBI fw_dynamic               M-mode 固件
      U-Boot proper                    S-mode U-Boot
      hifive-unmatched-a00.dtb         启动阶段 DTB
    hash-1 = sha256
  /images/update                       在当前 U-Boot 中执行
    data = unmatched-firmware-update.cmd
    type = script
    hash-1 = sha256
  /configurations/unmatched
    firmware = "uboot"
    loadables = "spl"
    script = "update"
```

因此 OpenSBI 没有单独的 SD 分区。它是内层 `u-boot.itb` 的一个 FIT 节点，更新
loader2 时会与 U-Boot proper 一起更新。

整个过程分为“只操作内存”和“写 SD 卡”两个阶段：

```text
tftpboot
  -> unmatched-firmware.itb 下载到 DDR 0x84000000
  -> 尚未修改 SD 卡

source 0x84000000#unmatched
  -> 校验并执行 /images/update
  -> imxtract 校验并提取 spl、uboot 到 DDR
  -> 检查目标 GPT 类型、位置和容量
  -> mmc write 开始修改 SD 卡
  -> mmc read + cmp.b 回读验证
```

`tftpboot` 成功只表示文件已经进入 DDR，不代表固件已经更新。真正的入口是
`source 0x84000000#unmatched`：`#unmatched` 选择
`/configurations/unmatched`，U-Boot 从它的 `script = "update"` 属性找到脚本节点。
设置 `verify=yes` 后，`source` 会校验脚本节点的 SHA-256；脚本中的 `imxtract`
又分别校验 `spl` 和 `uboot` 节点，然后才把数据复制到指定 DDR 地址。

### 构建端如何生成 FIT 和脚本

实现位于 `scripts/litebuild.py`：

- `_unmatched_firmware_update_cmd()` 返回板端执行的完整 U-Boot 脚本；
- `_unmatched_firmware_fit_its()` 生成外层 FIT 的 ITS 描述；
- `build_unmatched_firmware_fit()` 检查输入、补齐扇区、写出脚本和 ITS，再调用
  U-Boot 的 `mkimage` 生成 `deploy/unmatched-firmware.itb`。

打包前会先按本仓库的 SD 分区上限检查文件：SPL 不得超过 1 MiB，
`u-boot.itb` 不得超过 4 MiB。之后使用以下等价逻辑把每个文件补零到 512 字节
边界：

```python
data = artifact.read_bytes()
padded = data + bytes(-len(data) % 512)
```

这样 `mmc write` 写入的最后一个扇区也完全来自 FIT，不会把目标地址后面原有的
DDR 内容带入 SD 卡。补齐后的 SPL、`u-boot.itb` 和更新脚本分别放进 FIT 节点并
生成 SHA-256。核心打包过程等价于：

```python
update_script.write_text(_unmatched_firmware_update_cmd(), encoding="ascii")
its.write_text(
    _unmatched_firmware_fit_its(padded_spl, padded_uboot, update_script),
    encoding="ascii",
)
run([mkimage, "-f", its, "deploy/unmatched-firmware.itb"])
```

`./build.sh u-boot` 和 `./build.sh dev-uboot` 在生成新的 `u-boot-spl.bin` 和
`u-boot.itb` 后会自动重新打包固件 FIT。已有这两个输入文件时，也可以只执行：

```bash
./build.sh firmware-fit
```

### 板端更新脚本实现

构建生成的 `out/unmatched-firmware-update.cmd` 内容如下。它会作为
`/images/update` 节点原样进入 FIT：

```text
setenv firmware_addr_r 0x84000000 &&
setenv spl_update_addr_r 0x82000000 &&
setenv uboot_update_addr_r 0x82400000 &&
setenv firmware_verify_addr_r 0x83000000 &&
setenv firmware_mmcdev 0 &&
setenv firmware_spl_part 1 &&
setenv firmware_uboot_part 2 &&
setenv verify yes &&
imxtract ${firmware_addr_r} spl ${spl_update_addr_r} &&
setenv spl_update_size ${filesize} &&
setexpr spl_update_blocks ${filesize} + 0x1ff &&
setexpr spl_update_blocks ${spl_update_blocks} / 0x200 &&
imxtract ${firmware_addr_r} uboot ${uboot_update_addr_r} &&
setenv uboot_update_size ${filesize} &&
setexpr uboot_update_blocks ${filesize} + 0x1ff &&
setexpr uboot_update_blocks ${uboot_update_blocks} / 0x200 &&
mmc dev ${firmware_mmcdev} &&
part type mmc ${firmware_mmcdev}:${firmware_spl_part} spl_partition_type &&
part type mmc ${firmware_mmcdev}:${firmware_uboot_part} uboot_partition_type &&
itest.s ${spl_partition_type} == 5b193300-fc78-40cd-8002-e86c45580b47 &&
itest.s ${uboot_partition_type} == 2e54b353-1271-4842-806f-e436d6af6985 &&
part start mmc ${firmware_mmcdev} ${firmware_spl_part} spl_update_start &&
part size mmc ${firmware_mmcdev} ${firmware_spl_part} spl_partition_blocks &&
part start mmc ${firmware_mmcdev} ${firmware_uboot_part} uboot_update_start &&
part size mmc ${firmware_mmcdev} ${firmware_uboot_part} uboot_partition_blocks &&
itest ${spl_update_blocks} -le ${spl_partition_blocks} &&
itest ${uboot_update_blocks} -le ${uboot_partition_blocks} &&
echo Writing U-Boot and OpenSBI to MMC ${firmware_mmcdev}:${firmware_uboot_part}... &&
mmc write ${uboot_update_addr_r} ${uboot_update_start} ${uboot_update_blocks} &&
mmc read ${firmware_verify_addr_r} ${uboot_update_start} ${uboot_update_blocks} &&
cmp.b ${uboot_update_addr_r} ${firmware_verify_addr_r} ${uboot_update_size} &&
echo Writing SPL to MMC ${firmware_mmcdev}:${firmware_spl_part}... &&
mmc write ${spl_update_addr_r} ${spl_update_start} ${spl_update_blocks} &&
mmc read ${firmware_verify_addr_r} ${spl_update_start} ${spl_update_blocks} &&
cmp.b ${spl_update_addr_r} ${firmware_verify_addr_r} ${spl_update_size} &&
echo Firmware update verified - reset the board
```

脚本使用的 DDR 地址互不重叠：

| 变量 | 地址 | 内容 |
|---|---:|---|
| `spl_update_addr_r` | `0x82000000` | 从 FIT 提取的 SPL |
| `uboot_update_addr_r` | `0x82400000` | 从 FIT 提取的 `u-boot.itb` |
| `firmware_verify_addr_r` | `0x83000000` | MMC 回读比较缓冲区，可重复使用 |
| `firmware_addr_r` | `0x84000000` | TFTP 下载的外层固件 FIT |

`imxtract` 成功后会把当前子镜像长度写入 `${filesize}`。脚本必须在提取下一个节点
前保存 SPL 长度，在提取 `uboot` 后再保存 U-Boot 长度。写入扇区数使用：

```text
blocks = (filesize + 0x1ff) / 0x200
```

其中 `0x200` 是 512 字节，`0x1ff` 是 511，所以这是向上取整到完整扇区。当前
打包文件已经是 512 字节对齐，该计算仍保留为防御性检查。

脚本不使用固定的写入起始扇区。它通过 `part start` 和 `part size` 从目标 SD 卡的
实际 GPT 获取位置与容量，但要求目标仍是本仓库规定的分区角色：

| SD 分区 | FIT 节点 | GPT type GUID |
|---|---|---|
| `mmc 0:1` | `spl` | `5b193300-fc78-40cd-8002-e86c45580b47` |
| `mmc 0:2` | `uboot` | `2e54b353-1271-4842-806f-e436d6af6985` |

只有 GUID 和容量检查全部通过后，第一条 `mmc write` 才会执行。更新先写 loader2
中的 OpenSBI/U-Boot，完成回读比较后才写作为启动入口的 loader1 SPL。每次写入
后的检查过程都是：

```text
mmc write 源地址 分区起始扇区 扇区数
mmc read  回读地址 分区起始扇区 扇区数
cmp.b     源地址 回读地址 镜像字节数
```

所有命令使用 `&&` 串联。任意 hash、提取、MMC、GPT、容量、写入、读取或比较
失败，后续命令都不会执行，也不会打印最终成功信息。但该机制不提供事务回滚：
如果 loader2 已写入成功、随后 loader1 写入失败或设备断电，卡上可能出现新旧
版本混合；如果正在执行任意一次 `mmc write` 时断电，对应分区可能损坏。

### 新版 U-Boot 的简化入口

新版 U-Boot 默认环境中的 `tftp_update_firmware` 只负责下载并启动 FIT 脚本：

```text
tftp_update_firmware=
    echo WARNING: updating SPL, OpenSBI, and U-Boot on MMC 0 &&
    tftpboot ${firmware_addr_r} ${firmware_fit} &&
    setenv verify yes &&
    source ${firmware_addr_r}#unmatched
```

因此 `run tftp_update_firmware` 与手工执行 `tftpboot`、`setenv verify yes`、
`source` 使用的是同一份 FIT 内脚本，不存在两套不同的写卡实现。

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
