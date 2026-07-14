# PCIe 学习路线：基于 HiFive Unmatched 的 PCIe 实践计划

## Context

本项目（meson-unmatched）搭建了轻量级构建框架，覆盖 OpenSBI → U-Boot → Linux
的完整启动链。FU740 SoC 的 PCIe 控制器是 **Synopsys DesignWare (DWC) PCIe IP
v4.80a+** 的集成实现（unrolled iATU 模式），提供了从 bare metal 到用户空间的
完整学习路径：

```
bare metal → U-Boot SPL → U-Boot proper → Linux kernel → userspace
```

## 学习阶段概览

```
阶段 1: PCIe 协议基础 + FU740 硬件拓扑
阶段 2: U-Boot 层面的 PCIe（SPL 桥初始化 + U-Boot proper 枚举）
阶段 3: Linux PCIe 子系统剖析
阶段 4: 动手实验与调试（含 bare metal 方向）
阶段 5: 高级话题（可选）
```

---

## 阶段 1: PCIe 协议基础与硬件拓扑

### 1.1 PCIe 协议核心概念

| 概念 | 说明 | 在 FU740 上的体现 |
|------|------|------------------|
| **拓扑** | RC → Switch → EP | FU740 RC → ASM1042A PCIe Switch → x16 插槽 + M.2 |
| **BDF** | Bus:Device.Function 地址 | `pci enum` / `lspci` 输出 |
| **配置空间** | Type 0 (EP) / Type 1 (Bridge) Header + Capabilities | 通过 DBI 或 ECAM 窗口访问 |
| **ECAM** | 内存映射配置空间 | `config` reg region @ `0xd_f000_0000`, 256 MB |
| **BAR** | Base Address Register, 设备申请 MMIO/IO 资源 | U-Boot `pci_auto.c` 和 Linux `pci_assign_unassigned_bus_resources()` |
| **ATU** | Address Translation Unit (DWC 特有) | Unrolled iATU, 在 `pcie_dw_sifive.c:425` / `pcie-designware.c` 中编程 |
| **MSI/MSI-X** | 消息信号中断 | DWC 内置 MSI 控制器, PLIC IRQ 56 |
| **LTSSM** | Link Training and Status State Machine | `PCIEX8MGMT_APP_LTSSM_ENABLE` 寄存器控制 |
| **Gen1 quirk** | 初始强制 2.5 GT/s | Linux `pcie-fu740.c:186-200` 和 U-Boot `pcie_dw_sifive.c:270-293` 都有 |

### 1.2 HiFive Unmatched PCIe 硬件拓扑

```
FU740 SoC (RC)
  └─ PCIe Gen3 x8
       └─ ASM1042A PCIe Switch (via UBRDG_RSTN GPIO 7)
            ├─ x16 物理插槽 (电气 x8)
            ├─ M.2 M-Key (NVMe, x4 lanes)
            └─ M.2 E-Key (WiFi/BT, x1 lane + USB)
```

板级 GPIO：

- `GPIO 5` = `PCIe_PWREN` — PCIe 电源使能
- `GPIO 8` = `PCIe_PERSTN` — PERST# 复位信号
- `GPIO 7` = `UBRDG_RSTN` — USB/PCIe 桥接芯片 (ASM1042A) 复位

### 1.3 FU740 寄存器映射

从设备树 `src/u-boot/arch/riscv/dts/fu740-c000.dtsi:332-340`：

| reg-name | 物理地址 | 大小 | 用途 |
|----------|---------|------|------|
| `dbi` | `0xe_0000_0000` | 2 GB | DWC Data Bus Interface — RC 自身的配置空间 |
| `config` | `0xd_f000_0000` | 256 MB | ECAM 风格的下游设备配置空间 |
| `mgmt` | `0x0_100d_0000` | 4 KB | FU740 私有管理寄存器 (PHY, LTSSM, PERST) |

设备树 `ranges` 属性 (地址转换)：

```
I/O:          0x0_6008_0000 → PCI 0x0_6008_0000   (64 KB)
Non-prefetch: 0x0_6009_0000 → PCI 0x0_6009_0000   (~16 MB)
Non-prefetch: 0x0_7000_0000 → PCI 0x0_7000_0000   (256 MB)
Prefetchable: 0x20_0000_0000 → PCI 0x20_0000_0000  (128 GB, 64-bit)
```

### 1.4 DWC 控制器的三层架构

```
┌──────────────────────────────────────┐
│ SiFive 集成层 (pcie-fu740.c)          │  ← 357 行
│  - PHY 初始化 (8 lanes, AC term)      │
│  - 时钟/复位/GPIO 管理                │
│  - mgmt_base 寄存器 (LTSSM, PERST)    │
├──────────────────────────────────────┤
│ DWC 核心层 (pcie-designware*.c)       │  ← host: 1225 行, core: ~800 行
│  - DBI 读写 (RoW enable/disable)      │
│  - iATU 编程 (outbound/inbound)       │
│  - MSI 中断控制器                     │
│  - Link training 等待                 │
├──────────────────────────────────────┤
│ Linux PCI 核心 (drivers/pci/)         │
│  - 枚举 (probe.c)                     │
│  - 资源分配 (setup-bus.c)             │
│  - 驱动绑定 (pci-driver.c)            │
└──────────────────────────────────────┘
```

### 1.5 准备：构建并验证基础环境

```bash
./build.sh toolchain    # 一次性：编译 SiFive SDK 工具链
./build.sh check        # 验证工具链和宿主工具
./build.sh              # 构建完整 SD 镜像 → deploy/unmatched-lite.img
```

烧写并在板上验证启动链：

```bash
# U-Boot 阶段快速 PCIe 检查
pci enum              # 枚举 PCIe 总线
pci 0                 # 显示设备详情 (BDF, Vendor/Device ID, BAR)
nvme scan             # 扫描 NVMe 设备

# Linux 阶段快速 PCIe 检查
lspci -vvv            # 显示所有 PCIe 设备详细信息
dmesg | grep -iE 'pci|pcie|nvme'
cat /proc/iomem | grep -i pci
```

---

## 阶段 2: U-Boot 层面的 PCIe

### 2.1 U-Boot SPL 中的 PCIe 桥初始化

文件：`src/u-boot/board/sifive/unmatched/spl.c:118-121`

函数 `spl_usb_pcie_bridge_init()` 是启动过程中最早的 PCIe 相关代码：

- 通过 `UBRDG_RESET` GPIO (即 GPIO 7) 复位 ASM1042A PCIe Switch
- 在 U-Boot SPL 阶段执行，早于任何 PCIe 枚举

这是理解 "PCIe 复位层次" 的起点：ASM1042A Switch 必须先脱离复位，下游设备才能
被 RC 发现。

### 2.2 U-Boot Proper PCIe 枚举

文件：`src/u-boot/drivers/pci/pcie_dw_sifive.c` (506 行)

完整调用链：

```
pcie_sifive_probe()                      ← DM (Driver Model) probe
  ├─ pcie_sifive_init_port()             ← PHY + 控制器初始化
  │    ├─ GPIO: PERST# assert, PWREN enable
  │    ├─ clk_enable(&aux_ck)
  │    ├─ reset_deassert(&reset)
  │    ├─ PHY init (AC termination mode, all 8 lanes)
  │    ├─ 设置 device type = Root Complex (0x4)
  │    ├─ pcie_dw_setup_host()           ← DWC 通用 host 初始化
  │    ├─ pcie_sifive_force_gen1()       ← 强制 Gen1 (line 270-293)
  │    ├─ 使能 LTSSM
  │    └─ pcie_dw_wait_for_link()        ← 等待 link up
  ├─ pcie_dw_prog_outbound_atu()         ← 配置 ATU window 0
  └─ 打印 "PCIE-0: Link up (Gen3-x8, Bus0)"
```

随后 U-Boot PCI uclass (`pci-uclass.c`) 和 `pci_auto.c` 自动完成：

- 深度优先总线扫描
- BAR 大小探测与地址分配
- 设备驱动绑定（如 NVMe）

### 2.3 关键文件阅读顺序

| 序号 | 文件 | 行数 | 重点 |
|------|------|------|------|
| 1 | `src/u-boot/drivers/pci/pcie_dw_sifive.c` | 506 | FU740 集成: probe, PHY init, Gen1 quirk |
| 2 | `src/u-boot/drivers/pci/pcie_dw_common.h` | 168 | DWC 结构体, iATU 寄存器定义, link status |
| 3 | `src/u-boot/drivers/pci/pcie_dw_common.c` | ~200 | ATU 编程, config 读写, host setup |
| 4 | `src/u-boot/drivers/pci/pci-uclass.c` | ~600 | U-Boot PCI 框架: 枚举, 资源管理 |
| 5 | `src/u-boot/drivers/pci/pci_auto.c` | ~300 | 自动 BAR 分配算法 |
| 6 | `src/u-boot/board/sifive/unmatched/spl.c` | ~200 | SPL: PCIe bridge reset |

### 2.4 动手：U-Boot 命令行调试

```
# 查看 PCIe 树
pci enum
pci 0                    # 显示 bus 0 上的所有设备

# NVMe 操作
nvme scan
nvme info
nvme part

# 读取配置空间 (ECAM)
md 0xd00000000           # Bus 0, Device 0, Function 0 的 config space

# 读取 DBI 寄存器 (RC 自身配置)
md 0xe00000000

# 读取 mgmt 寄存器
md 0x100d0000            # PERST, LTSSM, device type 等

# 查看设备树中的 PCIe 节点
fdt addr ${fdtcontroladdr}
fdt print /soc/pcie@e00000000
```

### 2.5 动手：Gen1 链路训练实验

在 `pcie_dw_sifive.c:270-293` 的 `pcie_sifive_force_gen1()` 函数中：

1. 注释掉 Gen1 强制逻辑
2. 重新构建：`UNMATCHED_LITE_DIRTY_SRC=1 ninja -C builddir u-boot`
3. 观察通过 ASM1042A Switch 连接的设备是否能正常 link up
4. 理解：为什么需要 Gen1 启动？涉及 PCIe 链路训练的协议细节

---

## 阶段 3: Linux PCIe 子系统剖析

### 3.1 Linux 驱动的完整调用链

文件：`src/linux/drivers/pci/controller/dwc/pcie-fu740.c` (357 行)

```
fu740_pcie_probe()                       ← platform_driver probe
  ├─ 获取资源: dbi_base, mgmt_base, gpios, clocks
  ├─ fu740_pcie_enable_interrupts()
  ├─ fu740_pcie_host_init()              ← .host_init callback
  │    ├─ GPIO: PERST# assert → deassert 序列
  │    ├─ clk_prepare_enable(pcie_aux)
  │    └─ reset_control_deassert(rst)
  │
  └─ dw_pcie_host_init(&pci->pp)         ← 在 pcie-designware-host.c
       ├─ dw_pcie_setup_rc()             ← pcie-designware.c
       ├─ dw_pcie_msi_host_init()        ← MSI 控制器初始化
       ├─ pci_host_probe(bridge)         ← 触发 Linux PCI 核心枚举
       │    └─ pci_scan_child_bus()
       │         └─ pci_scan_slot()      ← 深度优先扫描 BDF
       │    └─ pci_assign_unassigned_bus_resources()
       │    └─ pci_bus_add_devices()
       │         └─ pci_bus_add_device()
       │              └─ device_attach()
       │                   └─ nvme_probe()  ← NVMe 驱动绑定!
       └─ .start_link callback:
            fu740_pcie_start_link()
              ├─ 强制 Gen1 (写 Link Capability 寄存器)
              ├─ PCIEX8MGMT_APP_LTSSM_ENABLE = 0x1
              ├─ dw_pcie_wait_for_link()
              └─ 可选: 重新协商到原生速度
```

### 3.2 关键文件阅读顺序

| 序号 | 文件 | 行数 | 重点 |
|------|------|------|------|
| 1 | `pcie-fu740.c` | 357 | FU740 shim: probe, host_init, start_link |
| 2 | `pcie-designware.h` | 967 | 核心结构体 (`dw_pcie`, `dw_pcie_rp`, `dw_pcie_ops`), 所有寄存器定义 |
| 3 | `pcie-designware.c` | ~800 | `dw_pcie_setup_rc()`, `dw_pcie_wait_for_link()`, iATU 编程 |
| 4 | `pcie-designware-host.c` | 1225 | `dw_pcie_host_init()`, MSI 中断处理 |
| 5 | `drivers/pci/probe.c` | ~3000 | Linux PCI 枚举核心: `pci_scan_child_bus()` |
| 6 | `drivers/pci/setup-bus.c` | ~1800 | 资源分配: `pci_assign_unassigned_bus_resources()` |
| 7 | `drivers/nvme/host/pci.c` | ~3000 | NVMe 驱动: 如何通过 PCIe 发现并驱动 NVMe 设备 |
| 8 | FU740 DTS: `fu740-c000.dtsi:332-363` | 30 | PCIe 节点: reg, ranges, interrupts |

### 3.3 动手：Linux 命令行观察

```bash
# === 拓扑与设备 ===
lspci -vvv -t                          # 树形拓扑
lspci -vvv -s 00:00.0                  # RC 详情
lspci -vvv -s 01:00.0                  # 第一个 EP 详情 (通常是 NVMe)
lspci -vvv -d 1b4b:                   # 按 Vendor ID 过滤 (Marvell NVMe)

# === 内核日志 ===
dmesg | grep -iE 'fu740|pci|pcie|nvme|msi|bar|bridge|ltssm'
dmesg | grep 'PCIE-'                   # U-Boot 打印的 link 信息

# === sysfs ===
ls /sys/bus/pci/devices/               # 所有 PCIe 设备
cat /sys/bus/pci/devices/0000:01:00.0/config  # 原始 256-byte 配置空间
hexdump -C /sys/bus/pci/devices/0000:01:00.0/config
cat /sys/bus/pci/devices/0000:01:00.0/resource  # BAR 分配

# === 内存映射 ===
cat /proc/iomem | grep -A 20 pci       # 查看 PCIe MMIO 窗口
cat /proc/ioports | grep pci           # IO 端口分配

# === 中断 ===
cat /proc/interrupts | grep -iE 'pci|nvme|msi'

# === 动态调试 (需要 CONFIG_DYNAMIC_DEBUG) ===
echo 'file pcie-fu740.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control
dmesg -w                               # 实时观察 PCIe 日志
```

### 3.4 动手：研究设备树

```bash
# 反编译构建生成的 DTB
dtc -I dtb -O dts deploy/hifive-unmatched-a00.dtb > unmatched.dts
grep -A 40 'pcie@e00000000' unmatched.dts

# 对照 U-Boot 源 DTS
cat src/u-boot/arch/riscv/dts/fu740-c000.dtsi | grep -A 35 'pcie@e00000000'
```

---

## 阶段 4: 动手实验与调试

### 4.1 实验: 追踪完整的 PCIe 枚举过程

在 **U-Boot** 中添加调试输出：

```c
// pcie_dw_sifive.c 的 pcie_sifive_probe() 中:
printf("FU740 PCIe: mgmt_base=0x%llx, dbi_base=0x%llx\n", ...);
printf("FU740 PCIe: LTSSM state = 0x%x\n", readl(mgmt_base + 0x10));
printf("FU740 PCIe: Link speed = Gen%d, width = x%d\n", speed, width);
```

构建与部署：

```bash
# 直接在 src/u-boot 中修改, 然后:
UNMATCHED_LITE_DIRTY_SRC=1 ninja -C builddir u-boot
# 手动替换 SD 卡上的 u-boot.itb
```

在 **Linux** 中使用 dynamic debug 避免重新编译：

```bash
echo 'file pcie-fu740.c +pflmt' > /sys/kernel/debug/dynamic_debug/control
```

### 4.2 实验: NVMe 设备全流程交互

```bash
nvme list
nvme id-ctrl /dev/nvme0 | head -40
nvme list-ns /dev/nvme0
nvme smart-log /dev/nvme0
fdisk -l /dev/nvme0n1
dd if=/dev/nvme0n1 of=/dev/null bs=1M count=100 iflag=direct status=progress

# 从 NVMe 启动 (U-Boot 中)
nvme scan
nvme part 0
fatls nvme 0:1
```

### 4.3 实验: 插入 PCIe 外设

内核已编译的 PCIe 设备驱动：

- `CONFIG_E1000E=y` — Intel 千兆网卡
- `CONFIG_R8169=y` — Realtek 千兆网卡
- `CONFIG_BRCMFMAC_PCIE=y` — Broadcom WiFi
- `CONFIG_ATH9K=m` 等 — 各种 WiFi 网卡
- `CONFIG_DRM_AMDGPU=m` / `CONFIG_DRM_RADEON=m` — AMD 显卡

插入设备后：

```bash
echo 1 > /sys/bus/pci/rescan
dmesg | tail -30                       # 观察驱动加载
lspci -vvv -s <new_bdf>                # 查看新设备详情
```

### 4.4 Bare Metal 方向

**最小 PCIe 初始化序列**（从 `pcie_dw_sifive.c` 和 `pcie-fu740.c` 提取）：

```
1.  使能 PCIe 电源 (GPIO 5 = PCIE_PWREN)
2.  使能 pcie_aux 时钟 (PRCI)
3.  复位 UBRDG (GPIO 7) — 复位 ASM1042A Switch
4.  释放 PCIe 控制器复位 (reset_control_deassert)
5.  设置 DBI RoW enable (PCIE_MISC_CONTROL_1_OFF = 0x8bc, BIT(0))
6.  PHY 初始化 (mgmt_base + 0x860~0x880)
7.  设置 device type = RC (mgmt_base + 0x708)
8.  编程 iATU outbound window (cfg0 type, for ECAM access)
9.  强制 Gen1 (写 Link Capability register @ DBI offset 0x7c)
10. 使能 LTSSM (mgmt_base + 0x10 = 0x1)
11. 等待 link up (轮询 DBI offset 0x80)
12. ECAM 扫描: 遍历 Bus 0..255, Dev 0..31, Func 0..7
    读 Vendor ID (offset 0x00), 若 != 0xFFFF 则设备存在
```

Bare metal 起点参考：

- OpenSBI 可以作为一个 bare metal 框架的起点 (FW_PAYLOAD 模式)
- `src/u-boot/board/sifive/unmatched/spl.c` — SPL 阶段初始化（最接近 bare metal）
- 关键寄存器地址：dbi @ `0xe_0000_0000`, config @ `0xd_f000_0000`, mgmt @ `0x0_100d_0000`

### 4.5 调试工具速查

```bash
# === U-Boot 层面 ===
md <addr> <count>          # 内存 dump (可读 ECAM/DBI/mgmt 寄存器)
mw <addr> <value>          # 内存写
pci enum                   # 重新枚举
pci 0 <bus>                # 显示特定总线
nvme scan / nvme info      # NVMe 操作
fdt print <node>           # 查看设备树

# === Linux 层面 ===
lspci -vvv -xxx            # 超级详细 + 完整配置空间 hex dump
setpci -s BDF CMD=0x406    # 直接写配置空间寄存器
echo 1 > /sys/bus/pci/rescan
echo function > /sys/kernel/debug/tracing/current_tracer
echo '*pci*' > /sys/kernel/debug/tracing/set_ftrace_filter
cat /sys/kernel/debug/tracing/trace
```

---

## 阶段 5: 高级话题

### 5.1 U-Boot vs Linux PCIe 枚举对比

| 方面 | U-Boot | Linux |
|------|--------|-------|
| 枚举范围 | 基本 BDF 扫描 + BAR 分配 | 完整枚举 + AER/ASPM/热插拔 |
| iATU | 单个 outbound MEM window | 多个动态管理的 window |
| MSI | 最小支持 | 完整 MSI/MSI-X + IRQ domain |
| Gen1 quirk | 永久强制 Gen1 | Gen1 初始 → 重新协商原生速度 |
| 独立性 | 独立初始化 | 独立初始化 (不依赖 U-Boot 结果) |

### 5.2 QEMU virt PCIe 对比

QEMU virt 使用 `pci-host-generic` (ECAM GPEX)，与 FU740 的 DWC 有显著差异：

| 特性 | FU740 (DWC) | QEMU virt (GPEX) |
|------|-------------|-----------------|
| 配置访问 | DBI + iATU config window | 纯 ECAM |
| ATU | 需要手动编程 | N/A |
| PHY | 需要初始化 | 不需要 |
| 驱动复杂度 | 357 行 shim | 通用驱动 |

```bash
# 带 NVMe 模拟的 QEMU:
qemu-system-riscv64 -M virt -smp 8 -m 2G \
  -bios deploy/qemu/fw_dynamic.elf \
  -kernel deploy/qemu/u-boot.bin \
  -drive file=deploy/qemu/qemu-lite.img,if=none,format=raw,id=rootdisk \
  -device virtio-blk-device,drive=rootdisk \
  -drive file=nvme-test.img,if=none,id=nvme0 \
  -device nvme,serial=test,drive=nvme0
```

### 5.3 OpenSBI 与 PCIe

- OpenSBI 的 domain 机制可以限制 S-mode 对 PCIe MMIO 区域的访问
- PMU 事件可以监控 PCIe 性能计数器
- FW_TEXT_START=0x80000000 与 PCIe MMIO 空间不冲突

### 5.4 PCIe 带宽与性能

```bash
dd if=/dev/nvme0n1 of=/dev/null bs=1M count=1000 iflag=direct status=progress
```

理论带宽：PCIe Gen3 x4 = 3.938 GB/s (NVMe M.2), Gen3 x8 = 7.877 GB/s (x16 slot)

---

## 附录: 其他 Linux 发行版与 Bare Metal 环境下载

### Linux 发行版

#### Ubuntu (官方支持，最成熟)

- **Ubuntu 24.04 LTS** (推荐):
  `https://cdimage.ubuntu.com/releases/24.04/release/`
  文件: `ubuntu-24.04-preinstalled-server-riscv64+unmatched.img.xz`

- **Ubuntu 22.04 LTS**:
  `https://cdimage.ubuntu.com/releases/22.04/release/`
  文件: `ubuntu-22.04-preinstalled-server-riscv64+unmatched.img.xz`

- 官方下载页: https://ubuntu.com/download/risc-v
- 安装教程: https://ubuntu.com/tutorials/how-to-install-ubuntu-on-risc-v-hifive-boards

> **注意**: 文件名必须包含 `+unmatched`，已内置 U-Boot 和 DTB。Ubuntu 25.10
> 起要求 RVA23S64 ISA，Unmatched 不再支持，24.04 LTS 是长期选择。

#### 其他发行版

| 发行版 | 地址 |
|--------|------|
| Debian | https://wiki.debian.org/InstallingDebianOn/SiFive/HiFiveUnmatched |
| openSUSE | https://en.opensuse.org/HCL:HiFive_Unmatched |
| Fedora | https://fedoraproject.org/wiki/Architectures/RISC-V |
| NixOS | https://github.com/zhaofengli/nixos-riscv64/releases |

### Bare Metal 环境

#### SiFive Freedom Metal / Freedom E SDK (官方，已归档)

- 仓库: https://github.com/sifive/freedom-e-sdk
- 状态: **2024年7月已归档 (DEPRECATED)**，不包含 HiFive Unmatched BSP
- 仅支持: HiFive1, HiFive Unleashed (FU540), QEMU targets

#### 本项目作为 Bare Metal 起点

本仓库已有的源码就是最好的 bare metal 参考：

| 组件 | Bare Metal 价值 |
|------|----------------|
| `src/u-boot/board/sifive/unmatched/spl.c` | SPL 阶段直接操作 GPIO、时钟、复位寄存器 |
| `src/u-boot/drivers/pci/pcie_dw_sifive.c` | 完整 DWC PCIe 初始化序列（PHY → ATU → LTSSM → 枚举） |
| `src/opensbi` | OpenSBI 作为 M-mode 固件，可基于 FW_PAYLOAD 开发自定义 payload |
| `src/linux/.../pcie-fu740.c` | 对照参考 Linux 侧的寄存器操作 |

Bare metal 最小启动路径：

1. 基于 OpenSBI FW_PAYLOAD 模式，用自定义 payload 替换 U-Boot
2. 或直接修改 `spl.c`，在 SPL 阶段编写 PCIe 实验代码
3. 寄存器地址从 DTS 获取：dbi @ `0xe_0000_0000`, config @ `0xd_f000_0000`, mgmt @ `0x0_100d_0000`

#### 其他 RISC-V Bare Metal 框架

- **RustSBI**: https://github.com/rustsbi/rustsbi — Rust 写的 SBI 实现
- **xv6-riscv**: https://github.com/mit-pdos/xv6-riscv — MIT 教学操作系统

---

## 本仓库代码 (推荐阅读顺序)

| 优先级 | 文件 | 说明 |
|--------|------|------|
| ★★★ | `src/linux/drivers/pci/controller/dwc/pcie-fu740.c` | FU740 Linux 驱动 (357 行) |
| ★★★ | `src/u-boot/drivers/pci/pcie_dw_sifive.c` | FU740 U-Boot 驱动 (506 行) |
| ★★★ | `src/u-boot/arch/riscv/dts/fu740-c000.dtsi:#pcie` | PCIe 设备树节点 |
| ★★☆ | `src/linux/drivers/pci/controller/dwc/pcie-designware.h` | DWC 结构体/寄存器 (967 行) |
| ★★☆ | `src/u-boot/drivers/pci/pcie_dw_common.h` | U-Boot DWC 通用代码 (168 行) |
| ★★☆ | `src/linux/drivers/pci/controller/dwc/pcie-designware.c` | DWC 通用实现 |
| ★★☆ | `src/linux/drivers/pci/controller/dwc/pcie-designware-host.c` | DWC host 模式 (1225 行) |
| ★☆☆ | `src/u-boot/board/sifive/unmatched/spl.c` | SPL: PCIe bridge reset |
| ★☆☆ | `src/u-boot/drivers/pci/pci_auto.c` | BAR 分配算法 |
| ★☆☆ | `configs/linux/unmatched_defconfig` | 内核 PCIe 配置参考 |

## 外部资料

- **PCI Express Base Specification** (PCI-SIG)
- **Synopsys DesignWare PCIe Controller Databook**
- **"PCI Express Technology"** (MindShare, Mike Jackson & Ravi Budruk)
- **Linux Kernel Documentation**: `Documentation/PCI/`
- **SiFive FU740-C000 Manual**
- **SiFive Forums**: https://forums.sifive.com

## 验证检查清单

| 阶段 | 验证标准 |
|------|---------|
| 1 | 能解释 BDF、ECAM、BAR、ATU、LTSSM 概念；能画出 FU740 PCIe 拓扑图；板上启动到 Linux |
| 2 | 能在 U-Boot 中执行 `pci enum`/`nvme scan` 并解释输出；能修改 `pcie_dw_sifive.c` 添加调试输出；理解 Gen1 quirk 原因 |
| 3 | 能解释 `lspci -vvv` 每行输出；能追踪 `dw_pcie_host_init()` 完整调用链；读懂 DTS ranges 的地址转换 |
| 4 | 能独立添加/应用调试补丁；能操作 NVMe 设备；能插入 PCIe 外设并观察驱动加载 |
| 5 | 能对比 U-Boot/Linux/QEMU 枚举行为；能进行 NVMe 性能测试；(可选) bare metal PCIe 初始化 |
