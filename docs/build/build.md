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

### Linux 开发模式

学习 PCIe 驱动或 host controller 时，使用 `dev-linux` 保留 Linux 工作树和
内核配置：

```bash
./build.sh dev-linux
# 修改 src/linux/ 中的 Linux 源码
./build.sh dev-linux
```

首次调用会获取固定 Linux revision 并应用仓库已有的 Unmatched 补丁。之后
`dev-linux` 不会执行 `git reset --hard` 或 `git clean`，因此 `src/linux/` 的
修改会保留；它也不会覆盖 `out/linux/.config`。

不要在保留实验修改期间执行普通 `./build.sh`、`./build.sh linux` 或
`./build.sh qemu`，这些可复现构建目标会重置对应源码树。完成实验后，将你改动
的文件导出为 patch：

```bash
git -C src/linux diff -- drivers/pci/ > patches/linux/0002-pcie-learning.patch
```

导出时只指定自己修改的路径。若改动与已有 `0001` 补丁修改同一个文件，需要先
人工检查生成的 diff，避免把已有补丁的内容重复写入新 patch。

### U-Boot 开发模式

学习 SPL、PCIe bridge 复位或 U-Boot 驱动时，使用 `dev-uboot` 保留
U-Boot 工作树和 `.config`：

```bash
./build.sh dev-uboot
# 修改 src/u-boot/ 中的 U-Boot 源码（如 drivers/pci/pcie_dw_sifive.c）
./build.sh dev-uboot
```

首次调用会获取固定 U-Boot revision 并应用仓库已有的 Unmatched 补丁。之后
`dev-uboot` 不会执行 `git reset --hard` 或 `git clean`，因此
`src/u-boot/` 的修改会保留；它也不会覆盖 `out/u-boot/.config`。

`dev-uboot` 与 `dev-linux` 可以独立使用——修改 U-Boot 和内核的 PCIe 驱动
互不干扰：

```bash
./build.sh dev-uboot         # 只重编 U-Boot
./build.sh dev-linux          # 只重编 Linux 内核
./build.sh                     # 完整重建 SD 镜像（会重置两者）
```

完成实验后导出 U-Boot patch：

```bash
git -C src/u-boot diff -- drivers/pci/ > patches/u-boot/2026.01/0006-pcie-debug.patch
```

**部署 U-Boot 到物理板：** 与 Linux 不同，U-Boot SPL 和 ITB 存放在 SD 卡的
raw GPT 分区中，不能用文件系统方式替换。最简单的方式是用 `dev-uboot` 重编后
再跑一次 `./build.sh` 生成完整镜像烧写，或者直接 dd 覆盖 raw 分区：

```bash
# 仅替换 boot 分区（extlinux + Image.gz + DTB）
sudo mount /dev/sdX3 /mnt/sdcard
sudo cp deploy/Image.gz /mnt/sdcard/
sudo cp deploy/hifive-unmatched-a00.dtb /mnt/sdcard/
sudo umount /mnt/sdcard

# 或完整烧写新镜像
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
`fit.itb` 文件中。当前仓库只有 QEMU profile 使用 FIT；物理 Unmatched 镜像仍
使用独立的 `Image.gz`、DTB、extlinux 配置和 ext4 rootfs 分区。

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
