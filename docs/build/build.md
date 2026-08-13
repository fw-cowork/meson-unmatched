# 构建说明

本仓库默认构建 SiFive HiFive Unmatched 物理板的 SD 卡镜像。QEMU 使用独立
profile，不模拟 FU740 ROM 和物理板的 SPL 装载过程。

首次构建前，请按 `README.md` 的 Dependencies 章节安装宿主工具、KAS 和
QEMU。

## 初始化 SiFive 工具链

首次执行时下载 SiFive Freedom-U-SDK `2026.01.00` 的源码和 Yocto 依赖，生成
Linux SDK 并安装到本仓库的 `toolchains/sifive/`：

```bash
cd /home/adrian/devel/riscv/meson-unmatched
./build.sh toolchain
./build.sh check
```

该步骤需要较大的磁盘空间和较长时间。工具链生成完成后，后续普通构建不再依赖
父目录中的 KAS 工程。

## 构建框架和目录

统一入口是 `build.sh`。它先用 Meson 配置构建目录，再由 Ninja 调用
`scripts/litebuild.py`；Python 驱动最后调用 OpenSBI、U-Boot、Linux 和
BusyBox 各自的 Makefile。`baremetal/` 中的轻量程序由 Meson 直接调用 cross
file 声明的编译器：

```text
./build.sh <target>
  -> meson + ninja
  -> scripts/litebuild.py -> component Makefile
  -> baremetal Meson target -> RISC-V compiler
```

构建过程中各目录的职责如下：

| 路径 | 用途 | 是否提交到本仓库 |
|---|---|---|
| `src/linux/` | 固定 Linux revision 的开发工作树 | 否 |
| `src/u-boot/` | 固定 U-Boot revision 的开发工作树 | 否 |
| `src/opensbi/` | 固定 OpenSBI revision 的工作树 | 否 |
| `out/` | `.config`、目标文件和其他中间产物 | 否 |
| `deploy/` | 可部署的最终产物 | 否 |
| `patches/` | 在固定上游 revision 上应用的持久化源码改动 | 是 |
| `configs/` | Linux 等组件的持久化配置 | 是 |
| `baremetal/` | U-Boot `go` 裸机程序、公共启动代码和链接规则 | 是 |

常用构建目标：

```bash
./build.sh opensbi-fw      # OpenSBI 固件
./build.sh u-boot          # U-Boot SPL + proper；缺少 OpenSBI 时也会编译它
./build.sh linux           # Linux Image.gz + Unmatched DTB
./build.sh fit             # 将已有 Image.gz 和 Unmatched DTB 打包为 FIT
./build.sh firmware-fit    # 将已有 SPL 和 u-boot.itb 打包为固件更新 FIT
./build.sh baremetal       # 全部 U-Boot go 裸机程序
./build.sh baremetal-test  # 主机上的裸机公共算法测试
./build.sh unmatched-led-artifacts # D2 RGB LED 的全部分析产物
./build.sh rootfs          # BusyBox rootfs
./build.sh bootchain       # OpenSBI + U-Boot + Linux
./build.sh                 # 完整物理板 SD 卡镜像
```

`deploy/` 中对应的主要产物为：

```text
fw_dynamic.bin
u-boot-spl.bin
u-boot.itb
unmatched-firmware.itb
Image.gz
hifive-unmatched-a00.dtb
unmatched-fit.itb
rootfs.ext4
unmatched-lite.img
```

裸机产物位于 Meson 构建目录，例如：

```text
builddir/baremetal/unmatched-led.elf
builddir/baremetal/unmatched-led.bin
builddir/baremetal/unmatched-led.map
builddir/baremetal/unmatched-led.dis
builddir/baremetal/unmatched-led.sym
builddir/baremetal/unmatched-led.check
```

裸机编译器、汇编器、链接器、`objcopy` 及 ISA/ABI 参数统一配置在 Meson cross
file；应用目录不保存工具链前缀或绝对路径。`build.sh` 会记录 cross file 的内容
指纹，配置内容变化时自动重新创建 Meson 构建目录，使新参数立即生效。

目录组织、添加新程序的方法和 U-Boot `go` 调用约定见
[`baremetal/README.md`](../../baremetal/README.md)。

### 可复现构建与开发构建

普通目标用于从仓库输入重新产生结果。编译对应组件前，它会将 `src/` 工作树恢复
到 `scripts/litebuild.py` 固定的上游提交，执行 `git reset --hard` 和
`git clean -fdx`，再应用 `patches/` 中登记的补丁。以下命令会丢弃对应源码树中
尚未导出为 patch 的修改：

```bash
./build.sh linux
./build.sh u-boot
./build.sh bootchain
./build.sh
./build.sh qemu
```

修改 Linux 或 U-Boot 源码时使用开发目标：

```bash
./build.sh dev-linux       # 保留 src/linux/ 和 out/linux/.config
./build.sh dev-uboot       # 保留 src/u-boot/ 和 out/u-boot/.config
```

开发目标适合反复编辑、编译和上板验证；验证完成后必须把源码差异整理到
`patches/`，才能由普通目标稳定复现。执行普通目标前先检查：

```bash
git -C src/linux status --short
git -C src/u-boot status --short
git -C src/opensbi status --short
```

当前没有 `dev-opensbi` 目标。`./build.sh opensbi-fw` 每次都会恢复
`src/opensbi/`，因此不要把未保存的 OpenSBI 实验修改交给这个目标。需要修改
OpenSBI 时，应先为 `scripts/litebuild.py` 增加 OpenSBI patch 和开发模式支持，
或者先把修改导出到主仓库后再接入构建流程。

### 使用现有 Yocto SDK

推荐先执行 `./build.sh toolchain`，之后始终使用上面的 `build.sh` 入口。当前机器
尚未在本仓库安装 `toolchains/sifive/`，本次重新编译 `Image.gz` 时临时复用了
`/home/adrian/devel/riscv/unmatched/sifiveinc-2026.01` 中已有的 Yocto SDK：

```bash
sdk=/home/adrian/devel/riscv/unmatched/sifiveinc-2026.01/build/tmp/work/\
unmatched-freedomusdk-linux/linux-sifive/6.18.3+git

python3 scripts/litebuild.py dev-linux \
  --cross-compile riscv64-freedomusdk-linux- \
  --sysroot "$sdk/recipe-sysroot" \
  --toolchain-bindir \
    "$sdk/recipe-sysroot-native/usr/bin/riscv64-freedomusdk-linux" \
  --native-bindir "$sdk/recipe-sysroot-native/usr/bin" \
  --native-sysroot "$sdk/recipe-sysroot-native"
```

该命令与 `./build.sh dev-linux` 一样保留当前 Linux 源码和 `.config`，只是显式
传入了工具链路径。路径依赖本机 Yocto 工作目录，不应作为长期构建接口。

## Unmatched 物理板

不带参数时构建物理板 SD 卡镜像：

```bash
./build.sh
```

构建过程会自动下载固定版本的 OpenSBI、U-Boot、Linux 和 BusyBox，并生成：

```text
deploy/unmatched-lite.img
```

镜像包含以下启动链：

```text
U-Boot SPL -> OpenSBI FW_DYNAMIC -> U-Boot -> Linux Image.gz + DTB -> BusyBox rootfs
```

### U-Boot TFTP 启动与 OpenSBI 打印

完整操作流程和故障排查见 [U-Boot TFTP 启动](../boot/tftp-boot.md)。

Unmatched 构建把 `CONFIG_SPL_OPENSBI_SCRATCH_OPTIONS` 设置为 `0x2`。SPL 因此
不会再要求 OpenSBI 隐藏启动信息，并会打开 OpenSBI 的 runtime debug prints。
这不同于 OpenSBI 的 `DEBUG=1`：后者关闭编译优化，用于源码调试，并不是日志
开关。

### SPL 详细启动日志

默认的 Unmatched SPL 只打印版本和 `Trying to boot from MMC1`。本仓库额外启用：

```text
CONFIG_LOG=y
CONFIG_LOG_MAX_LEVEL=4
CONFIG_LOG_DEFAULT_LEVEL=4
CONFIG_SPL_LOG=y
CONFIG_SPL_LOG_MAX_LEVEL=7
CONFIG_SPL_SHOW_ERRORS=y
```

`CONFIG_SPL_LOG_MAX_LEVEL=7` 把 debug 日志编译进 SPL；板级初始化在串口可用后把
SPL 的运行时级别调到 7。全局的 `CONFIG_LOG_DEFAULT_LEVEL` 仍为 4，因此不会
同时把 U-Boot proper 变成 debug 输出。level 8 的内容转储和 level 9 的寄存器
I/O 也不会编译进 SPL。

重编并更新 `unmatched-firmware.itb` 后，串口输出将包含以下类型的信息：

```text
U-Boot SPL 2026.01-dirty (...)
HiFive Unmatched SPL debug logging enabled
Initializing FU740 DRAM
FU740 DRAM initialized
Initializing PWM, Ethernet PHY, and USB resets
Board peripheral resets complete
Boot mode select: register=0x..., mode=0x...
Trying to boot from MMC1
boot_device=..., mmc_dev=...
Selecting MMC dev ...
Found FIT
...
Loaded OpenSBI: os=OpenSBI, load=0x..., entry=0x..., size=0x..., fdt=0x...
Handing control to RISC-V OpenSBI
```

这些信息来自 SPL 自身，发生在 U-Boot 环境加载之前，不能用 `setenv` 临时打开。
修改后执行 `./build.sh dev-uboot`，再按 TFTP 固件更新流程写入新的 SPL；具体命令
见 [U-Boot TFTP 启动](../boot/tftp-boot.md)。

U-Boot 默认环境提供 `tftp_boot`，通过 TFTP 一次加载包含内核和 DTB 的 FIT，
rootfs 仍使用 SD 卡第 4 分区。先构建并将 FIT 放进 TFTP server root：

```bash
./build.sh bootchain
cp deploy/unmatched-fit.itb /srv/tftp/
```

烧写本次生成的 SD 镜像后，在 U-Boot 命令行执行：

```text
=> run tftp_boot
```

默认静态网络参数如下：

```text
serverip=192.168.1.23
ipaddr=192.168.1.24
netmask=255.255.255.0
```

`ipaddr` 是 Unmatched 板卡地址，`serverip` 是 TFTP 服务器地址。`tftp_boot`
不使用 DHCP；它把 `unmatched-fit.itb` 一次加载到 `${fit_addr_r}`，设置启动参数，
最后执行：

```text
bootm ${fit_addr_r}#unmatched
```

FIT 内的 kernel 节点把压缩内核加载和入口地址设为 `0x80200000`，fdt 节点携带
`hifive-unmatched-a00.dtb`。FIT 自身加载到 `0x84000000`，不会与解压后的内核
重叠。kernel 和 fdt 都带 SHA-256 hash，由 U-Boot 在启动时校验。

每次 `./build.sh u-boot` 或 `./build.sh dev-uboot` 完成后，还会自动生成：

```text
deploy/unmatched-firmware.itb
```

它把补齐到 512 字节边界的 `u-boot-spl.bin`、包含 OpenSBI 的 `u-boot.itb` 和板端
更新脚本打包在一起，三个节点分别带 SHA-256。已有 U-Boot 产物时也可只重新打包：

```bash
./build.sh firmware-fit
```

板端使用方法、GPT 校验、回读验证和断电恢复见
[U-Boot TFTP 启动](../boot/tftp-boot.md)。

默认启动参数是：

```text
root=/dev/mmcblk0p4 rw rootwait console=ttySIF0,115200 earlycon=sbi loglevel=8
```

需要排查自动命令时，可以手工执行同样的 TFTP 启动步骤：

```text
=> setenv serverip 192.168.1.23
=> setenv ipaddr 192.168.1.24
=> setenv netmask 255.255.255.0
=> setenv fit_addr_r 0x84000000
=> tftpboot ${fit_addr_r} unmatched-fit.itb
=> setenv bootargs root=/dev/mmcblk0p4 rw rootwait console=ttySIF0,115200 earlycon=sbi loglevel=8
=> bootm ${fit_addr_r}#unmatched
```

如果 SPI Flash 中已有持久化的旧 U-Boot environment，新加入的默认变量可能不
会自动出现。`unmatched-firmware.itb` 只更新 SD 卡中的固件，不清除 SPI Flash
environment。可只恢复这些变量，不影响 `ethaddr`、`serial#` 和其他自定义项：

```text
=> env default fit_addr_r ipaddr serverip netmask tftp_boot tftp_fit tftp_bootargs firmware_addr_r firmware_fit tftp_update_firmware
=> printenv tftp_boot tftp_fit fit_addr_r serverip ipaddr netmask
=> saveenv
```

OpenSBI 打印发生在进入 U-Boot proper 之前，所以只替换 TFTP 目录中的 Linux
FIT 不会改变 OpenSBI 输出；应使用 `unmatched-firmware.itb` 更新卡上的 SPL 和
`u-boot.itb`，或重新烧写完整 SD 镜像。

### 修改 Linux

首次进入开发模式时先构建一次，然后修改 `src/linux/` 下的源码。例如修改
FU740 PCIe 驱动和 PCI 枚举核心：

```bash
./build.sh dev-linux

vim src/linux/drivers/pci/controller/dwc/pcie-fu740.c
vim src/linux/drivers/pci/probe.c

./build.sh dev-linux
```

首次调用会获取固定 Linux revision 并应用仓库已有的 Unmatched 补丁。之后
`dev-linux` 不会执行 `git reset --hard` 或 `git clean`，因此 `src/linux/` 的
修改会保留；它也不会覆盖 `out/linux/.config`。构建成功后更新：

```text
deploy/Image.gz
deploy/hifive-unmatched-a00.dtb
```

使用 TFTP 启动时，`dev-linux` 会在 U-Boot `mkimage` 已存在时同步刷新 FIT。将
这个文件更新到服务器即可：

```bash
sudo cp deploy/unmatched-fit.itb /srv/tftp/
```

不要在保留实验修改期间执行普通 `./build.sh`、`./build.sh linux` 或
`./build.sh qemu`，这些可复现构建目标会重置对应源码树。

`dev-linux` 应用仓库补丁时不会在 `src/linux/` 中创建提交，因此 `git diff`
同时包含已有补丁和后续实验修改。若修改的是已有 `0002` 覆盖的 PCIe 文件，应
重新生成 `0002`，而不是创建一个与它内容重叠的 `0003`：

```bash
git -C src/linux diff -- \
  drivers/pci/Kconfig \
  drivers/pci/controller/dwc/Kconfig \
  drivers/pci/controller/dwc/pcie-fu740.c \
  drivers/pci/probe.c \
  > patches/linux/6.18/0002-pcie-fu740-debug-trace.patch
```

若修改的是任何现有补丁都没有覆盖的新文件，可以只对新文件生成下一编号的补丁，
再在 `scripts/litebuild.py` 的 `fetch()` 和 `check()` 中登记。无论采用哪种方式，
都应先检查主仓库中的 patch diff，再用普通 `./build.sh linux` 验证它能从固定
revision 干净应用并完成编译。

修改内核配置时先编辑 `out/linux/.config`，使用 `dev-linux` 验证。确认后把需要
长期保留的选项同步到 `configs/linux/unmatched_defconfig`，否则普通构建会恢复
仓库 defconfig。

### 修改 U-Boot

首次进入开发模式时先构建一次，再修改 `src/u-boot/`。例如：

```bash
./build.sh dev-uboot

vim src/u-boot/drivers/pci/pcie_dw_sifive.c
vim src/u-boot/board/sifive/unmatched/unmatched.env

./build.sh dev-uboot
```

首次调用会获取固定 U-Boot revision 并应用仓库已有的 Unmatched 补丁。之后
`dev-uboot` 不会执行 `git reset --hard` 或 `git clean`，因此
`src/u-boot/` 的修改会保留；它也不会覆盖 `out/u-boot/.config`。

`dev-uboot` 与 `dev-linux` 可以独立使用——修改 U-Boot 和内核的 PCIe 驱动
互不干扰：

```bash
./build.sh dev-uboot         # 只重编 U-Boot
./build.sh dev-linux         # 只重编 Linux 内核
```

构建成功后更新：

```text
deploy/u-boot-spl.bin
deploy/u-boot.itb
```

U-Boot 工作树同样同时包含仓库补丁和实验修改。若继续修改 PCIe 代码，应把
`0006` 原来覆盖的全部文件重新导出到原补丁：

```bash
git -C src/u-boot diff -- \
  drivers/pci/Kconfig \
  drivers/pci/pci-uclass.c \
  drivers/pci/pci_auto.c \
  drivers/pci/pcie_dw_common.c \
  drivers/pci/pcie_dw_sifive.c \
  > patches/u-boot/2026.01/0006-pcie-fu740-debug-trace.patch
```

若修改默认 TFTP 环境或 OpenSBI scratch options，则重新生成 `0007`：

```bash
git -C src/u-boot diff -- \
  board/sifive/unmatched/unmatched.env \
  configs/sifive_unmatched_defconfig \
  > patches/u-boot/2026.01/0007-unmatched-tftp-and-opensbi-prints.patch
```

只有修改旧补丁没有覆盖的新文件时，才创建下一编号的补丁并在
`scripts/litebuild.py` 中登记。归档后用普通 `./build.sh u-boot` 验证干净构建。
修改 U-Boot 配置时先编辑 `out/u-boot/.config` 并用 `dev-uboot` 验证，最终把
选项写入 U-Boot defconfig patch 或由构建脚本显式设置。

**部署 U-Boot 到物理板：** 与 Linux 不同，U-Boot SPL 和 ITB 存放在 SD 卡的
raw GPT 分区中，不能通过 TFTP 或 boot 文件系统替换。开发阶段可以把两个文件
直接写入已卸载 SD 卡的对应分区：

```bash
lsblk -o NAME,SIZE,TYPE,MOUNTPOINTS
sudo dd if=deploy/u-boot-spl.bin of=/dev/sdX1 bs=1M conv=fsync
sudo dd if=deploy/u-boot.itb of=/dev/sdX2 bs=1M conv=fsync
```

`/dev/sdX1` 和 `/dev/sdX2` 必须是目标 SD 卡的 SPL、U-Boot 分区，执行前必须用
`lsblk` 核对，且不能处于挂载状态。写错设备会破坏其他磁盘。

将修改导出并接入 patch 流程后，才使用普通目标生成可复现的完整镜像：

```bash
./build.sh
sudo dd if=deploy/unmatched-lite.img of=/dev/sdX bs=4M status=progress
```

### PCIe 初始化与枚举追踪输出

有三个独立的 Kconfig 选项控制不同层级的 trace，可以按需组合：

| 选项 | 范围 | 适用平台 | 默认 |
|---|---|---|---|
| `PCIE_ENUM_DEBUG` (Linux) | **枚举核心**：BDF 扫描、BAR sizing、总线拓扑 | QEMU + Unmatched | y |
| `PCIE_FU740_DEBUG` (Linux) | **FU740 驱动**：PERST/PHY/LTSSM/DBI 寄存器 | Unmatched 专用 | y |
| `PCIE_ENUM_DEBUG` (U-Boot) | **枚举核心**：BDF 扫描、BAR 分配、bridge 配置 | QEMU + Unmatched | y |
| `PCIE_DW_SIFIVE_DEBUG` (U-Boot) | **FU740 驱动**：PERST/PHY/LTSSM/DBI + DWC host | Unmatched 专用 | y |

**QEMU 用户：** 只需 `PCIE_ENUM_DEBUG=y` 即可看到完整的枚举 trace——QEMU
的 GPEX host bridge 不经过 FU740 驱动步骤。Unmatched 物理板开启全部四个
可以看到从 GPIO 复位到枚举完成的完整流程。

**U-Boot 阶段（21 步 + 枚举扫描）：**

```text
fu740-pcie: [Step  1] of_to_plat: parsing DT resources
fu740-pcie: [Step  2] GPIOs: reset + pwren OK
fu740-pcie: [Step  3] pcie_aux clock acquired
fu740-pcie: [Step  4] PRCI reset control acquired
fu740-pcie: [Step  5] probe: calling pcie_sifive_init_port(RC)
fu740-pcie: [Step  6] PERST# assert (GPIO low + controller PERST_N=0)
fu740-pcie: [Step  7] slot power on (PWREN GPIO=1) + 100ms hold
fu740-pcie: [Step  8] PERST# deassert (controller + GPIO high)
fu740-pcie: [Step  9] enabling pcie_aux clock
fu740-pcie: [Step 10] hold PHY reset (APP_HOLD_PHY_RST=1)
fu740-pcie: [Step 11] deassert PRCI power_up_rst_n
fu740-pcie: [Step 12] init_phy: programming 8 lanes AC termination
fu740-pcie: [Step 13] PHY done
fu740-pcie: [Step 14] release PHY reset (APP_HOLD_PHY_RST=0) + re-enable pcie_aux
fu740-pcie: [Step 15] set device_type = Root Complex (0x4)
fu740-pcie: [Step 16] call pcie_dw_setup_host() → setup RC + iATU
pci-enum: [22] setup_host: configure RC BARs + interrupt + bus
fu740-pcie: [Step 17] force Gen1: enable DBI RoW + write LNKCAP
fu740-pcie: [Step 18] enable LTSSM (mgmt+0x10 = 1)
fu740-pcie: [Step 19] polling PORT_DEBUG1 for link up (timeout=20)
fu740-pcie:   PHY_DEBUG_R1=0x... [up=0 train=1] left=20   ← 每次轮询都打印!
fu740-pcie:   PHY_DEBUG_R1=0x... [up=0 train=1] left=19
  ...
fu740-pcie:   PHY_DEBUG_R1=0x... [up=1 train=0] left=12   ← link up!
fu740-pcie: [Step 20] init_port done: speed=Gen1 width=x8 bus=0
PCIE-0: Link up (Gen1-x8, Bus0)
fu740-pcie: [Step 21] programming iATU outbound MEM window
pci-enum: [23] bind bus 0: scanning BDFs...
pci-enum:   00:00.0: Vendor=0xf15e Device=0x0000 class=0x060400 hdr=1  ← RC
pci-enum:   01:00.0: Vendor=0x1b4b Device=0x1092 class=0x010601 hdr=0  ← NVMe
pci-enum: [24] bind done: 2 device(s) on bus 0
pci-enum: [25] setup_host done: MEM phys=0x... bus=0x... size=0x...
```

**Linux 阶段（21 步初始化 + 14 步枚举）：**

```text
fu740-pcie: [Step  1] probe: platform driver matched         ← 步骤 1-21 与 U-Boot 相同
  ...
fu740-pcie: [Step 21] link up at native speed
pci-enum: [29] === ENUMERATION START ===
pci-enum: [30] root-bus scan done — bus=00
pci-enum: [31] scan bus 00 (start=0)
pci-enum:   probe 0000:00:00.0: Vendor=0xf15e Device=0x0000 class=0x060400 hdr=1 BRIDGE
pci-enum:   probe 0000:00:01.0: empty                      ← 不存在
  ...
pci-enum:   probe 0000:01:00.0: Vendor=0x1b4b Device=0x1092 class=0x010801 hdr=0 ENDP
pci-enum: [33] 0000:01:00.0 BAR0 sizing: type=0
pci-enum: [34] 0000:01:00.0: size=0x4000 flags=0x...
pci-enum: [33] 0000:01:00.0 BAR1 sizing: type=0
  ...
pci-enum: [32] bus 00 scan done — max_subordinate=01
pci-enum: [39] assigning unassigned BAR resources
pci-enum: [40] BAR assignment done
pci-enum: [41] adding devices to driver model
pci-enum: [42] === ENUMERATION COMPLETE ===
```

**步骤编号有意不连续以对应调用层级：** 1-4 为资源获取，5-21 为
`host_init`/`start_link`（U-Boot 与 Linux 两侧步骤 6-21 镜像），
22-28 为 Linux 独有的 `dw_pcie_host_init` 内部子步骤，29-42 为
Linux PCI 核心枚举。U-Boot 的步骤 22-25 对应 DWC host setup 和 DM 绑定。

**完整步骤对照表：**

| Step | Linux | U-Boot | 操作 |
|---|---|---|---|
| 1 | probe: driver matched | of_to_plat: parse DT | 驱动入口 / DT 资源解析 |
| 2 | GPIOs acquired (reset/pwren) | GPIOs: reset + pwren OK | GPIO 获取 |
| 3 | pcie_aux clock acquired | pcie_aux clock acquired | 时钟 |
| 4 | PRCI reset control | PRCI reset control | PRCI 复位 |
| 5 | → host_init: bring-up start | probe: calling init_port(RC) | 进入 init_port |
| 6 | assert PERST# | assert PERST# | GPIO + mgmt PERST=0 |
| 7 | enable slot power (100ms) | slot power on (100ms) | PWREN=1 |
| 8 | deassert PERST# | deassert PERST# | PERST=1, GPIO high |
| 9 | enable pcie_aux clock | enable pcie_aux clock | 时钟使能 |
| 10 | hold PHY reset (APP_HOLD=1) | hold PHY reset | PHY 保持在复位 |
| 11 | deassert PRCI rst_n | deassert PRCI rst_n | 释放 PRCI 复位 |
| 12 | init_phy: 8 lanes | init_phy: 8 lanes | CR_PARA AC 终端 |
| 13 | PHY done | PHY done | — |
| 14 | release PHY + re-enable clk | release PHY + re-enable clk | APP_HOLD=0 |
| 15 | *(in start_link)* | set device_type=RC | RC 模式 |
| *(15)* | start_link: read LNKCAP | *(in start_link)* | 读取 Link Cap |
| 16 | force Gen1 (LNKCAP write) | force Gen1 (LNKCAP write) | 强制 2.5GT/s |
| 17 | enable LTSSM | enable LTSSM | mgmt+0x10=1 |
| 18 | poll PORT_DEBUG1 ... | poll PORT_DEBUG1 ... | 链路训练等待 |
| 19 | link up! (Gen1) | link up! | 训练完成 |
| 20 | re-training at native speed | *(n/a: U-Boot stays Gen1)* | 二阶速度协商 |
| 21 | link up at native speed | init_port done | — |
| — | ***(gap: Linux dw_pcie_host_init)*** | program iATU MEM window | U-Boot iATU |
| 22 | dw_pcie_host_init: get resources | setup_host: DBI writes | Linux DWC / U-Boot DBI |
| 23 | calling .host_init | *(n/a)* | callback |
| 24 | DWC IP version + iATU type | — | 版本检测 |
| 25,25a-c | setup_rc: DBI BAR0/INT/BUS/CMD/CLASS + iATU windows | — | RC 配置详情 |
| 25 | — | setup_host done | MEM/cfg 地址摘要 |
| 26 | link not up → start_link | — | 触发 link start |
| 27 | calling pci_host_probe() | — | 进入枚举 |
| — | — | — | — |
| 29 | === ENUMERATION START === | *(U-Boot: pci_bind_bus_devices)* | 枚举入口 |
| 30 | root-bus scan done | — | bus 初始化 |
| 31 | scan bus 00 | [23] bind bus N: scanning BDFs... | 开始扫描 |
| | probe 0000:00:00.0: Vend=... class=... BRIDGE | 00:00.0: Vend=... class=... | **每个 BDF 打印** |
| | probe 0000:00:01.0: empty | 01:00.0: empty | 无设备 |
| | probe 0000:01:00.0: Vend=... class=... ENDP | *(U-Boot 在上方循环内打印)* | 发现设备 |
| 33-34 | BAR0 sizing → size=0xXXX flags=... | — | BAR 探测 |
| 32 | bus 00 scan done — max=01 | [24] bind done: N device(s) | 扫描完成 |
| 39 | assigning unassigned BARs | *(pci_auto.c 内部)* | 资源分配 |
| 40 | BAR assignment done | — | — |
| 41 | adding devices to driver model | — | 驱动绑定 |
| 42 | === ENUMERATION COMPLETE === | — | 枚举结束 |

## QEMU

**QEMU 上的 trace 输出（启用 `PCIE_ENUM_DEBUG`）：**

QEMU virt 用 GPEX host bridge，没有 FU740 的 PERST/PHY/LTSSM 步骤。
trace 直接从枚举核心开始：

```text
pci-enum: [29] === ENUMERATION START ===
pci-enum: [30] root-bus scan done — bus=00
pci-enum: [31] scan bus 00 (start=0)
pci-enum:   probe 0000:00:00.0: Vendor=0x1b36 Device=0x0008 class=0x060000 hdr=0 ENDP  ← host bridge
pci-enum:   probe 0000:00:01.0: Vendor=0x1af4 Device=0x1001 class=0x010000 hdr=0 ENDP  ← virtio-blk
pci-enum:   probe 0000:00:02.0: Vendor=0x1b36 Device=0x0010 class=0x010600 hdr=0 ENDP  ← NVMe
pci-enum:   probe 0000:00:03.0: empty
  ...
pci-enum: [33] 0000:00:01.0 BAR0 sizing: type=0
pci-enum:   0000:00:01.0: size=0x200 flags=0x20203
pci-enum: [33] 0000:00:02.0 BAR0 sizing: type=0
pci-enum:   0000:00:02.0: size=0x4000 flags=0x20203
pci-enum: [32] bus 00 scan done — max_subordinate=00
pci-enum: [39] assigning unassigned BAR resources
pci-enum: [40] BAR assignment done
pci-enum: [41] adding devices to driver model
pci-enum: [42] === ENUMERATION COMPLETE ===
```

**Unmatched 物理板上的完整 trace** 则包含了 FU740 驱动步骤（Step 1-28），
详见上文 U-Boot / Linux 阶段的完整示例。

## QEMU

```bash
./build.sh qemu
./qemu.sh
```

也可以由启动脚本负责构建：

```bash
./qemu.sh --build
```

自动验证启动时使用超时参数：

```bash
./qemu.sh --timeout 24
```

QEMU 的启动链为：

```text
OpenSBI FW_DYNAMIC -> QEMU S-mode U-Boot -> boot.scr -> FIT -> Linux -> BusyBox
```

`deploy/qemu/fit.itb` 包含内核、QEMU virt DTB 和 BusyBox CPIO rootfs；
`deploy/qemu/qemu-lite.img` 的 FAT 分区只包含 FIT 和 `boot.scr`。

### FIT 格式

FIT 是 Flattened Image Tree。它将多个启动组件和它们的元数据放入单个
`fit.itb` 文件中。QEMU 使用 `deploy/qemu/fit.itb`；物理 Unmatched 的 TFTP
流程使用 `deploy/unmatched-fit.itb`。物理板 SD boot 分区仍保留独立的
`Image.gz`、DTB 和 extlinux 配置。

QEMU 构建会生成 `out/qemu/fit.its`，再使用本次构建的 U-Boot `mkimage` 工具
生成 `deploy/qemu/fit.itb`。FIT 的默认配置名为 `qemu`，包含：

```text
kernel   deploy/qemu/Image.gz          Linux 内核，加载和入口地址为 0x80200000
fdt      out/qemu/qemu-virt.dtb        与当前 8 CPU、2 GiB QEMU virt 配置匹配的 DTB
ramdisk  deploy/qemu/rootfs.cpio.gz    BusyBox initramfs
```

每个 FIT 子镜像都有 SHA-256 哈希。U-Boot 在加载时验证哈希，任意一个校验失败
都会停止启动。`rootfs.cpio.gz` 在 FIT 中作为未再压缩的数据传给 Linux，由内核
处理其 gzip CPIO 格式。

`boot.scr` 的实际启动逻辑为：

```text
fatload virtio 0:1 0x84000000 fit.itb
setenv bootargs console=ttyS0 earlycon=sbi
bootm 0x84000000#qemu
```

因此 U-Boot 读取单个 FIT，并根据 `qemu` 配置同时取得内核、DTB 和 initramfs。
检查 FIT 内容：

```bash
out/qemu/u-boot/tools/mkimage -l deploy/qemu/fit.itb
```

## QEMU GDB 调试

启动 QEMU 并在复位处暂停：

```bash
./qemu-gdb.sh --build
```

在另一个终端连接：

```bash
gdb-multiarch deploy/qemu/fw_dynamic.elf
```

```gdb
target remote 127.0.0.1:1234
continue
```

默认 GDB server 只监听 `127.0.0.1:1234`。使用其他端口：

```bash
./qemu-gdb.sh --port 1235
```

## 清理

删除构建和部署产物，同时保留已下载源码：

```bash
ninja -C builddir clean-lite
```

需要重新下载所有组件时，再手动删除 `src/`、`downloads/` 或
`toolchains/sifive/`。
