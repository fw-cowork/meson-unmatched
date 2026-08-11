# Linux on HiFive Unmatched

本文档从 Linux 内核的角度描述在 HiFive Unmatched 上的启动、驱动、设备树和
开发工作流。这是对分散在构建指南、PCIe 分析和 DTS 文件中的 Linux 知识的整合。

## 内核配置

本项目的 Linux defconfig 位于 `configs/linux/unmatched_defconfig`，
针对 Unmatched 学习目的定制。

### 架构与平台

```kconfig
CONFIG_RISCV=y
CONFIG_64BIT=y
CONFIG_MMU=y
CONFIG_SOC_SIFIVE=y
CONFIG_ARCH_SIFIVE=y
CONFIG_RELOCATABLE=y             # 运行时重定位（可选）
CONFIG_PAGE_OFFSET=0xff60000000000000  # 内核虚拟地址基址
```

### 启动时必须内置的驱动（=y，非模块）

```kconfig
CONFIG_SERIAL_SIFIVE=y            # SiFive UART (ttySIF0)
CONFIG_SERIAL_SIFIVE_CONSOLE=y    # 用作控制台
CONFIG_MMC_SPI=y                  # SPI MMC (SPI Flash 上的 SD 卡)
CONFIG_EXT4_FS=y                  # ext4 根文件系统
CONFIG_VFAT_FS=y                  # FAT boot 分区
CONFIG_DEVTMPFS=y                 # 设备节点自动创建
CONFIG_BINFMT_ELF=y               # ELF 可执行文件（BusyBox）
CONFIG_BINFMT_SCRIPT=y            # #!/bin/sh 脚本
```

### PCIe 子系统

```kconfig
CONFIG_PCI=y
CONFIG_PCIEPORTBUS=y              # PCIe 端口服务 (AER, PME, hotplug)
CONFIG_PCI_HOST_GENERIC=y         # 通用 host 桥 (QEMU virt 备用)
CONFIG_PCIE_FU740=y               # FU740 DesignWare PCIe 驱动
CONFIG_PCIE_DW=y                  # DesignWare 通用层
CONFIG_PCI_MSI=y                  # 消息信号中断
CONFIG_BLK_DEV_NVME=m             # NVMe 驱动 (模块)
```

**设计考量：**
- `PCIE_FU740=y` 内置，因为在 `ext4` rootfs 可用之前 PCIe 控制器就需初始化
- `BLK_DEV_NVME=m` 模块，因为 rootfs 挂载后才加载（rootfs 在 MMC 上，不在 NVMe）

### 外设驱动（已编译但非核心）

```kconfig
CONFIG_E1000E=y                   # Intel 千兆网卡 (PCIe)
CONFIG_R8169=y                    # Realtek 千兆网卡 (PCIe)
CONFIG_BRCMFMAC_PCIE=y            # Broadcom WiFi (PCIe)
CONFIG_SATA_AHCI=y                # AHCI SATA 控制器 (PCIe)
CONFIG_DRM_RADEON=m               # AMD Radeon GPU (PCIe)
CONFIG_DRM_AMDGPU=m               # AMD GPU (PCIe)
CONFIG_DRM_NOUVEAU=m              # NVIDIA GPU (PCIe)
```

### 调试选项

```kconfig
CONFIG_EARLY_PRINTK=y             # 早期打印（MMU 启用前）
CONFIG_DYNAMIC_DEBUG=y            # 运行时动态调试
CONFIG_MAGIC_SYSRQ=y              # SysRq 魔术键
```

## 设备树

### 源文件层次

```text
src/linux/arch/riscv/boot/dts/sifive/
  fu740-c000.dtsi                 ← SoC 级定义 (CPU, 总线, 外设)
  hifive-unmatched-a00.dts         ← 板级定义 (包含 fu740-c000.dtsi)
```

`fu740-c000.dtsi` 提供 SoC 级别的外设描述，`hifive-unmatched-a00.dts`
覆盖板级配置（内存大小、板载器件、GPIO 分配）。

### 关键节点说明

#### 内存节点

```dts
memory@80000000 {
    device_type = "memory";
    reg = <0x0 0x80000000 0x4 0x00000000>;  // 16 GiB
};
```

Linux 在该节点描述的基础上还会排除 reserved-memory 区域。

#### CPU 节点

```dts
cpus {
    #address-cells = <1>;
    #size-cells = <0>;
    cpu@0 { ... };  // U74 核心 0
    cpu@1 { ... };  // U74 核心 1
    cpu@2 { ... };  // U74 核心 2
    cpu@3 { ... };  // U74 核心 3
    cpu@4 { ... };  // S7 监控核心
};
```

5 个 hart：4 个 U74 应用核心 + 1 个 S7 监控核心。

#### 中断控制器

```dts
plic: interrupt-controller@c000000 {
    compatible = "sifive,plic-1.0.0";
    reg = <0x0 0xc000000 0x0 0x4000000>;  // PLIC
    interrupts-extended = <&cpu0_intc 11 &cpu0_intc 9 ...>;
    riscv,ndev = <69>;                      // 69 个中断源
};

clint: clint@10000000 {
    compatible = "riscv,clint0";
    reg = <0x0 0x10000000 0x0 0x10000>;    // CLINT
    interrupts-extended = <&cpu0_intc 3 &cpu0_intc 7 ...>;
};
```

#### 串口

```dts
uart0: serial@10010000 {
    compatible = "sifive,fu740-c000-uart", "sifive,uart0";
    reg = <0x0 0x10010000 0x0 0x1000>;
    interrupts = <42>;                       // PLIC IRQ 42
};
```

控制台用 `console=ttySIF0,115200`。

#### PCIe 控制器

```dts
pcie@e00000000 {
    compatible = "sifive,fu740-pcie";
    reg = <0xe 0x00000000 0x0 0x80000000     // dbi
           0xd 0xf0000000 0x0 0x10000000     // config
           0x0 0x100d0000 0x0 0x00001000>;   // mgmt
    reg-names = "dbi", "config", "mgmt";

    pwren-gpios = <&gpio 5 0>;               // PCIe 电源
    reset-gpios = <&gpio 8 0>;               // PERST#
    clocks = <&prci PRCI_CLK_PCIEAUX>;
    resets = <&prci PRCI_RST_PCIE_POWER_UP_N>;

    ranges = <0x81000000 0x0 0x60080000 0x0 0x60080000 0x0 0x10000   // I/O
              0x82000000 0x0 0x60090000 0x0 0x60090000 0x0 0xff0000   // MEM (NP)
              0x82000000 0x0 0x70000000 0x0 0x70000000 0x0 0x10000000 // MEM (NP)
              0xc3000000 0x20 0x00000000 0x20 0x00000000 0x20 0x00000000>; // Prefetchable
};
```

**ranges 解析：**

| flags | CPU 物理地址 | PCI 总线地址 | 大小 | 用途 |
|---|---|---|---|---|
| `0x81000000` | `0x60080000` | `0x60080000` | 64 KB | I/O 空间 |
| `0x82000000` | `0x60090000` | `0x60090000` | ~16 MB | Non-prefetchable MEM |
| `0x82000000` | `0x70000000` | `0x70000000` | 256 MB | Non-prefetchable MEM |
| `0xc3000000` | `0x20_00000000` | `0x20_00000000` | 128 GB | Prefetchable MEM (GPU BAR) |

## RISC-V Linux 早期启动

### 入口约定

```text
从 U-Boot 跳转到 Linux 时:
  a0 = boot hart ID        (U-Boot 设置: gd->arch.boot_hart)
  a1 = DTB 物理地址         (通常是 0x88000000)
  M-mode = OpenSBI 运行中   (SBI 服务可用)
  S-mode = 禁用 MMU         (物理地址模式)
```

### head.S 启动序列

```text
_start_kernel (arch/riscv/kernel/head.S)
  ├─ 保存 a0 (hart ID)、a1 (DTB 指针)
  ├─ 清除 BSS
  ├─ 设置 early 页表 (Sv39, 2MB 大页)
  │    PGD → PMD → 2MB 大页
  │    映射内核镜像 + DTB + early 内存区域
  ├─ 启用 MMU (satp 寄存器)
  ├─ 切换到虚拟地址运行
  ├─ start_kernel() (init/main.c)
  │    ├─ setup_arch()
  │    │    ├─ 解析 DTB
  │    │    ├─ 初始化 earlycon (通过 SBI)
  │    │    ├─ 设置 memblock (从 DTB memory 节点)
  │    │    └─ 探测平台设备
  │    ├─ 初始化中断 (PLIC)
  │    ├─ 初始化定时器 (CLINT via SBI)
  │    ├─ 初始化驱动
  │    │    ├─ sifive-serial → ttySIF0
  │    │    ├─ dwc2-pcie → fu740
  │    │    ├─ mmc_spi → mmc0
  │    │    └─ ...
  │    ├─ 挂载 rootfs
  │    └─ 执行 init
```

### earlycon 机制

`earlycon=sbi` 在内核启动参数中指定，利用 OpenSBI 的 `sbi_console_putchar()`
在 MMU 启用之前输出日志。一旦 `sifive-serial` 驱动加载完毕，
`console=ttySIF0` 接管控制台输出。

## Linux PCIe 驱动层级

### 三层架构

```text
┌─────────────────────────────────────┐
│ pcie-fu740.c (357 行)                │ ← 顶层: FU740 shim
│  - .host_init: GPIO 复位/电源/时钟   │
│  - .start_link: Gen1 强制/LTSSM      │
│  - .init_phy: CR_PARA lane 配置      │
├─────────────────────────────────────┤
│ pcie-designware-host.c (1225 行)     │ ← 中间层: DWC host 通用
│  - dw_pcie_host_init()               │
│  - MSI 中断控制器初始化               │
│  - iATU 窗口管理                     │
├─────────────────────────────────────┤
│ pcie-designware.c (≈800 行)          │ ← 底层: DWC 核心
│  - dw_pcie_setup_rc()                │
│  - dw_pcie_wait_for_link()           │
│  - iATU 编程                         │
├─────────────────────────────────────┤
│ Linux PCI 核心 (drivers/pci/)        │ ← 标准层
│  - 枚举 (probe.c)                    │
│  - 资源分配 (setup-bus.c)            │
│  - 驱动绑定 (pci-driver.c)           │
└─────────────────────────────────────┘
```

### probe 调用链

```text
fu740_pcie_probe()                      ← platform_driver probe
  ├─ 获取资源: dbi_base, mgmt_base, gpios, clocks, reset
  ├─ fu740_pcie_enable_interrupts()
  └─ dw_pcie_host_init(&pci->pp)        ← DWC 通用入口
       ├─ dw_pcie_host_get_resources()  ← 解析 DT reg/ranges/interrupts
       ├─ fu740_pcie_host_init()        ← .host_init 回调
       │    ├─ GPIO: PERST# assert → 100ms delay → deassert
       │    ├─ 使能 pcie_aux 时钟
       │    ├─ fu740_pcie_init_phy()    ← CR_PARA AC 终端配置
       │    └─ 释放 PRCI 复位
       ├─ dw_pcie_msi_host_init()       ← MSI 控制器
       ├─ dw_pcie_version_detect()      ← DWC IP 版本检测
       ├─ dw_pcie_setup_rc()            ← Root Complex 配置
       ├─ fu740_pcie_start_link()       ← .start_link 回调
       │    ├─ 强制 Gen1 (写 LNKCAP)
       │    ├─ 使能 LTSSM
       │    ├─ dw_pcie_wait_for_link()  ← 等待 link-up
       │    └─ 恢复到原生 link 速度
       ├─ dw_pcie_wait_for_link()
       └─ pci_host_probe(bridge)        ← 触发枚举 + 驱动绑定
            ├─ pci_scan_child_bus()
            ├─ pci_assign_unassigned_bus_resources()
            └─ pci_bus_add_devices()
                 └─ nvme_probe(), ahci_probe(), ...
```

### Gen1 → 原生速度的两阶段启动

```text
步骤 1: 强制 Gen1 (2.5 GT/s)
  └─ 写入 PCI_EXP_LNKCAP 寄存器的 Max Link Speed 字段 = 1

步骤 2: 使能 LTSSM，等待 link-up
  └─ ASM1042A 桥在 Gen1 下完成链路训练（兼容性最好）

步骤 3: 恢复 LNKCAP 中的原始最大速度
  └─ 发起 speed change request
  └─ 再次等待 link-up（现在应协商到更高速度）

原因: ASM1042A 桥和某些端点需要 Gen1 初始训练才能稳定
```

## BusyBox Rootfs

### 构建过程

```text
BusyBox 1.37.0 tarball
  ├─ 使用 SiFive SDK 工具链静态编译
  ├─ 安装 applet 符号链接 (CONFIG_INSTALL_APPLET_SYMLINKS)
  ├─ 覆盖 rootfs/ 目录中跟踪的文件
  │    etc/inittab
  │    etc/init.d/rcS
  └─ mke2fs -d <rootfs_tree> rootfs.ext4
```

### rootfs 结构

```text
rootfs.ext4 (ext4, BusyBox)
├── /bin/
│   ├── busybox             ← 静态链接的 BusyBox 二进制
│   ├── sh → busybox
│   ├── ls → busybox
│   ├── cat → busybox
│   └── ...                 ← 全部 applet 符号链接
├── /sbin/
│   └── init → /bin/busybox
├── /etc/
│   ├── inittab             ← init 配置
│   └── init.d/
│       └── rcS             ← 启动脚本 (挂载 proc/sys/dev)
├── /dev/                   ← devtmpfs 自动填充
├── /proc/                  ← procfs 挂载点
└── /sys/                   ← sysfs 挂载点
```

### init 脚本内容

`/etc/inittab`:

```text
::sysinit:/etc/init.d/rcS
::respawn:-/bin/sh
```

`/etc/init.d/rcS`:

```sh
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev
```

启动后进入 BusyBox shell。

## 开发工作流

### Linux 实验模式：dev-linux

本框架提供了标准的可重现构建和开发迭代两种模式：

**可重现构建：**

```bash
./build.sh linux    # git reset --hard + clean + defconfig → 确定性构建
```

**开发迭代模式：**

```bash
./build.sh dev-linux
# 首次: fetch + 应用 patches + defconfig
# 之后: 不 reset、不 clean、不覆盖 .config
# 你的源码修改保留在 src/linux/
./build.sh dev-linux    # 增量重编译
```

**导出 patch：**

```bash
git -C src/linux diff -- drivers/pci/ > patches/linux/0002-pcie-learning.patch
```

### 内核配置实验

```bash
# 首次构建后保留配置
./build.sh linux

# 修改配置
cd out/linux
make -C ../../src/linux O=$PWD ARCH=riscv CROSS_COMPILE=riscv64-sifive-linux- menuconfig

# 用修改后的配置重建
UNMATCHED_LITE_KEEP_CONFIG=1 ./build.sh linux
```

### 动态调试

无需重编译即可追踪 PCIe 或其他驱动：

```bash
# 查看所有可用调试站点
cat /sys/kernel/debug/dynamic_debug/control | grep pcie

# 启用 FU740 PCIe 驱动调试
echo 'file pcie-fu740.c +pflmt' > /sys/kernel/debug/dynamic_debug/control

# 启用所有 DWC 代码调试
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control

# 实时观察日志
dmesg -w
```

### 设备树实验

```bash
# 反编译运行时 DTB
dtc -I dtb -O dts deploy/hifive-unmatched-a00.dtb > current.dts

# 对照源码 DTS
diff -u src/linux/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts current.dts
# (fixup 会导致差异：/memory, /chosen 等)
```

## 常见验证命令

### 启动后检查

```bash
uname -a                            # 内核版本
dmesg | grep -iE 'sbi|pci|pcie|nvme|mmc|serial'
lspci -vvv                          # PCIe 设备详情
cat /proc/iomem | grep -E 'System RAM|pci'
cat /proc/interrupts | grep -iE 'pci|nvme|msi'
free -h                             # 可用内存
df -h                               # 磁盘使用
```

### PCIe 专项检查

```bash
lspci -vvv -t                       # 树形拓扑
lspci -nn                           # 数字 Vendor/Device ID
cat /sys/bus/pci/devices/0000:01:00.0/config | xxd  # 256-byte 配置空间
cat /sys/bus/pci/devices/0000:01:00.0/resource        # BAR 分配
ls /sys/bus/pci/drivers/            # 已加载 PCIe 驱动
```

### NVMe 专项检查

```bash
nvme list
nvme id-ctrl /dev/nvme0 | head -40
nvme smart-log /dev/nvme0
dd if=/dev/nvme0n1 of=/dev/null bs=1M count=100 iflag=direct status=progress
```

## 相关文档

- [构建说明](../build/build.md) — SD 镜像、QEMU、Linux 开发模式
- [启动链总览](../boot/boot-chain-overview.md) — 从 SPL 到 Linux 的完整流程
- [U-Boot 启动日志分析](../boot/uboot-boot-log.md) §6-7 — 内核重定位与 FDT 传递
- [PCIe 学习路线](../pcie/pcie-learning.md) §3 — Linux PCIe 驱动深入
- [PCIe 学习笔记](../pcie/pcie-study.md) §7 — Linux PCIe flow 代码级分析
- [内存布局参考](../reference/memory-map.md) — 全部物理地址速查
