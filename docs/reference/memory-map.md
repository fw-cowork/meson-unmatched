# HiFive Unmatched 内存布局参考

本文档整合分散在 SPL 分析、U-Boot 启动日志和 PCIe 笔记中的全部物理地址，
提供统一的速查参考。

## 地址空间总览

FU740 使用 64 位地址空间，物理地址有效位 40 位（1 TB）。关键区域：

```text
0x0000_0000_0000 ─┬────────────────────────────
                  │
  0x0_0001_0000    ├─ On-chip 外设 (MMIO)
  0x0_7000_0000    ├─ PCIe non-prefetchable MMIO
  0x0_d_f000_0000   ├─ PCIe ECAM 配置空间
  0x0_e_0000_0000   ├─ PCIe DBI (RC 自身配置)
                  │
  0x0_8000_0000    ├─ DDR 起始 (16 GB)
  0x4_8000_0000    ├─ DDR 结束
                  │
  0x20_0000_0000   ├─ PCIe prefetchable MMIO (128 GB)
                  │
  ...             ─┴────────────────────────────
```

## On-Chip 外设地址映射

### 系统控制器

| 地址 | 寄存器 | 用途 |
|---|---|---|
| `0x0_0000_1000` | MSEL | 启动模式选择 (Mode Select) |
| `0x0_0100_0000` | PRCI | 时钟与复位控制器 |
| `0x0_1000_0000` | CLINT | 核间定时器与软件中断 |
| `0x0_C000_0000` | PLIC | 平台级中断控制器 |

### GPIO 与引脚复用

| 地址 | 外设 | 关联 GPIO |
|---|---|---|
| `0x0_1006_0000` | GPIO | 外设复位控制 |
| GPIO 5 | `PCIe_PWREN` | PCIe 电源使能 |
| GPIO 7 | `UBRDG_RSTN` | USB/PCIe 桥接芯片 (ASM1042A) 复位 |
| GPIO 8 | `PCIe_PERSTN` | PERST# 复位信号 |
| GPIO 9 | `USB_ULPI_RSTN` | USB3320C ULPI PHY 复位 |
| GPIO 11 | `USB_HUB_RSTN` | ASM1074 USB Hub 复位 |
| GPIO 12 | `GbE_PHY_RSTN` | VSC8541 千兆 PHY 复位 |

### 外设控制器

| 地址 | 外设 |
|---|---|
| `0x0_1001_0000` | UART0 (调试串口) |
| `0x0_1001_1000` | UART1 |
| `0x0_1003_0000` | I2C0 (EEPROM) |
| `0x0_1004_0000` | QSPI0 (SPI Flash) |
| `0x0_1004_1000` | QSPI1 |
| `0x0_1004_2000` | QSPI2 |
| `0x0_1005_0000` | SD/eMMC (MMC0) |
| `0x0_1007_0000` | OTP (一次性可编程存储器) |
| `0x0_1009_0000` | Ethernet (GEMGXL) |
| `0x0_100B_0000` | DDR 控制器/PHY |
| `0x0_100B_8000` | DDR Physical Filter |
| `0x0_100D_0000` | PCIe mgmt 寄存器 |
| `0x0_2000_0000` | SPI Flash 映射地址 (QSPI XIP) |
| `0x0_3000_0000` | L2 LIM (on-chip SRAM, 2MB) |

### PWM

| 地址 | 外设 | 用途 |
|---|---|---|
| `0x0_1002_0000` | PWM0 | 3 色 LED + 1 风扇 |
| `0x0_1002_1000` | PWM1 | 3 路风扇控制 |

## DDR 内存空间

### 物理内存

```text
DDR 基址:    0x0_8000_0000
DDR 大小:    16 GB (0x4_0000_0000)
DDR 结束:    0x4_8000_0000
DDR 类型:    DDR4, 64-bit 宽度
DDR 速率:    ≈1866 MT/s (DDRPLL = 933333324 Hz)
```

### DDR 控制器寄存器

| 地址 | 偏移 | 用途 |
|---|---|---|
| `0x0_100B_0000` | 基址 | Denali DDR 控制器 (2 KB) |
| `0x0_100B_2000` | +0x2000 | Denali DDR PHY (8 KB) |
| `0x0_100B_8000` | +0x8000 | Physical Filter / Bus Blocker (4 KB) |

**控制器关键寄存器（相对偏移）：**

| 偏移 | 寄存器 | 用途 |
|---|---|---|
| `0x000` | `DENALI_CTL_0` | 控制器控制 (bit 0 = start) |
| `0x210` | `DENALI_CTL_132` | 初始化状态 (`MC_INIT_COMPLETE` 标志) |

### DDR 设备树节点

```dts
dmc: dmc@100b0000 {
    compatible = "sifive,fu740-c000-ddr";
    reg = <0x0 0x100b0000 0x0 0x0800      // 控制器
           0x0 0x100b2000 0x0 0x2000      // PHY
           0x0 0x100b8000 0x0 0x1000>;    // Physical Filter
    clocks = <&prci PRCI_CLK_DDRPLL>;
    clock-frequency = <933333324>;
};

memory@80000000 {
    device_type = "memory";
    reg = <0x0 0x80000000 0x4 0x00000000>;  // 16 GiB
};
```

## PCIe 地址空间

### FU740 PCIe 寄存器（来自设备树）

| reg-name | 物理地址 | 大小 | 用途 |
|---|---|---|---|
| `dbi` | `0x0_e_0000_0000` | 2 GB | DWC Data Bus Interface — RC 自身的 Type 0 配置空间 |
| `config` | `0x0_d_f000_0000` | 256 MB | ECAM 风格的下游设备 Type 0/1 配置空间 |
| `mgmt` | `0x0_0100_d_0000` | 4 KB | FU740 私有管理寄存器 (PHY, LTSSM, PERST 控制) |

### mgmt 寄存器关键偏移

| 偏移 | 寄存器 | 用途 |
|---|---|---|
| `0x000` | `PCIEX8MGMT_PERST_N` | 端点 PERST# 复位 |
| `0x010` | `PCIEX8MGMT_APP_LTSSM_ENABLE` | LTSSM 链路训练使能 |
| `0x01C` | `PCIEX8MGMT_APP_HOLD_PHY_RST` | 保持 PHY 复位（PHY 编程时使用） |
| `0x708` | `PCIEX8MGMT_DEVICE_TYPE` | 设备类型 (Root Complex=0x4, Endpoint=0x0) |
| `0x860–0x880` | `PCIEX8MGMT_PHY0_CR_PARA_*` | PHY0 CR_PARA 编程接口 |
| `0x8E0–0x900` | `PCIEX8MGMT_PHY1_CR_PARA_*` | PHY1 CR_PARA 编程接口 |

### DBI 寄存器关键偏移（DesignWare 标准）

| 偏移 | 寄存器 | 用途 |
|---|---|---|
| `0x07C` | `PCI_EXP_LNKCAP` | Link Capability (写此寄存器强制 Gen1) |
| `0x080` | `PCI_EXP_LNKSTA` / `PCIE_PORT_DEBUG1` | Link Status: bit4=link up, bit29=in training |
| `0x8BC` | `PCIE_MISC_CONTROL_1_OFF` | RoW enable (BIT(0) 使能只读寄存器写入) |

### PCIe MMIO 窗口（来自设备树 ranges）

```text
I/O 空间:         0x0_6008_0000 → PCI 0x0_6008_0000   (64 KB)
Non-prefetchable: 0x0_6009_0000 → PCI 0x0_6009_0000   (~16 MB)
Non-prefetchable: 0x0_7000_0000 → PCI 0x0_7000_0000   (256 MB)
Prefetchable:     0x20_0000_0000 → PCI 0x20_0000_0000  (128 GB, 64-bit)
```

**解读：** CPU 访问 `0x0_6008_0000` 开始的一段地址，iATU 将其翻译为 PCIe I/O 事务；
访问 `0x20_0000_0000` 开始的一段地址，翻译为 PCIe Memory 事务。

## SPL 启动时内存布局

| 地址 | 宏/常量 | 用途 |
|---|---|---|
| `0x08000000` | `SPL_TEXT_BASE` | SPL 代码段基址 (L2 LIM/SRAM) |
| `0x081CFE60` | `SPL_STACK` | SPL 栈顶 (SRAM 内) |
| `0x100000` (1MB) | `SPL_MAX_SIZE` | SPL 最大尺寸限制 |
| `0x80000000` | `SPL_OPENSBI_LOAD_ADDR` | OpenSBI firmware 加载地址 (DDR) |
| `0x80200000` | `TEXT_BASE` | U-Boot proper 入口地址 |
| `0x84000000` | `SPL_LOAD_FIT_ADDRESS` | FIT 镜像加载缓冲 (DDR) |
| `0x85000000` | `SPL_BSS_START_ADDR` | SPL BSS 段 (DDR) |

## Linux 启动时内存布局（内核重定位前后）

### 加载阶段（U-Boot 控制下）

```text
0x84000000: kernel_addr_r  = Image.gz 加载位置 (7.25 MB 压缩)
0x88000000: fdt_addr_r     = DTB 加载位置 (10 KB)
```

### 解压与重定位后

```text
0x80000000 ─┬──────────────────────
0x80200000   ├─ Linux Image 入口     ← text_offset=0x200000 (2MB)
             │  解压后 ≈14.7 MB
0x810B2000   ├─ Image 结束            (end=810b2000)
             │
0x84000000   ├─ 旧加载地址 (可重用)
             │
0x88000000   ├─ DTB (≈22 KB fixup 后)
0x880058E8   ├─ DTB 结束
             │
             │  供内核使用的剩余 DDR
0x4_8000_0000 ─┴──────────────────────
```

**关键计算：** `0x810b2000 - 0x80200000 = 0xE92000 ≈ 14.7 MB`

### 内核重定位不变量

| 不变量 | 值 | 原因 |
|---|---|---|
| `text_offset` | `0x200000` (2MB) | 从 Image Header 读取 |
| 2MB 对齐 | 必须 | Sv39 页表大页粒度 |
| 入口约定 | `a0 = boot_hart`, `a1 = dtb_addr` | RISC-V Linux 标准 |

## SPI Flash 布局（ISSI 32MB）

```text
0x00000000 ─┬──────────────────────
            │  U-Boot SPL
0x00010000   ├─ env 环境变量 (可选)
0x00100000   ├─ FIT Image (.itb)
            │  ├─ OpenSBI (fw_dynamic)
            │  ├─ U-Boot proper
            │  └─ DTB
            │
0x02000000  ─┴──────────────────────  (32MB 结束)
```

## SD 卡 GPT 分区布局（典型）

```text
GPT Header
├─ p1: SPL raw 区域 (固定 LBA 偏移)
├─ p2: U-Boot ITB raw 区域
├─ p3: boot 分区 (FAT32 或 ext4)
│      ├─ Image.gz
│      ├─ hifive-unmatched-a00.dtb
│      └─ extlinux/extlinux.conf
└─ p4: rootfs (ext4, BusyBox)
       ├─ /bin/busybox
       ├─ /etc/inittab
       ├─ /etc/init.d/rcS
       └─ /dev/
```

## QEMU virt 内存布局（对比）

| 方面 | 物理板 | QEMU virt |
|---|---|---|
| DDR 起始 | `0x80000000` | `0x80000000` |
| OpenSBI | `fw_dynamic.bin` (SPL 加载) | `fw_dynamic.elf` (QEMU -bios) |
| U-Boot | S-mode via SPL+OpenSBI | S-mode via QEMU -kernel |
| 内核加载 | extlinux FAT 分区 | FIT 镜像 (bootm) |
| PCIe | DWC via fu740 驱动 | GPEX via pci-host-generic |
| 串口 | `ttySIF0` @ `0x10010000` | `ttyS0` @ virtio |

## 宿主工具速查

### U-Boot 命令行

```text
bdinfo                    ← 查看内存布局
md.l 0x100b0000 8         ← 读取 DDR 控制器寄存器
md.l 0x100d0000 4         ← 读取 PCIe mgmt 寄存器
md 0xd00000000            ← 读取 PCIe ECAM (Bus 0, Dev 0, Func 0)
md 0xe00000000            ← 读取 PCIe DBI
fdt addr ${fdtcontroladdr}  ← 设置 FDT 地址
fdt print /memory@80000000 ← 查看内存节点
```

### Linux 命令行

```bash
cat /proc/iomem | grep -E 'System RAM|pci'
cat /proc/ioports | grep pci
dmesg | grep -E 'memblock|memory|OF: fdt'
free -h
```

## 相关文档

- [启动链总览](../boot/boot-chain-overview.md) — 各阶段如何使用这些内存区域
- [U-Boot SPL 代码解析](../boot/spl-analysis.md) §1 — SPL 内存地址布局详解
- [U-Boot 启动日志分析](../boot/uboot-boot-log.md) §6 — 内核重定位内存计算
- [PCIe 学习路线](../pcie/pcie-learning.md) §1.3 — FU740 寄存器映射
- [PCIe 学习笔记](../pcie/pcie-study.md) — DDR bring-up 与 PCIe SerDes 地址
