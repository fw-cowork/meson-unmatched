# HiFive Unmatched Boot Chain Overview

本文档提供从加电到 Linux 用户空间的完整启动链总览，将分散在
[SPL 分析](spl-analysis.md)、[U-Boot 启动日志](uboot-boot-log.md) 和
[PCIe 学习笔记](../pcie/pcie-study.md) 中的知识串联起来。

## 启动链五阶段

```text
ZSBL (ROM, M-mode)
  → U-Boot SPL (M-mode, on-chip SRAM)
    → OpenSBI (M-mode, DRAM)
      → U-Boot proper (S-mode, DRAM)
        → Linux kernel (S-mode, DRAM)
          → BusyBox init (S-mode, user space)
```

## 阶段 1：ZSBL — Zeroth Stage Boot Loader

**运行位置：** FU740 芯片内 ROM（只读，不可修改）
**运行模式：** M-mode
**内存：** 仅 L2 LIM (on-chip SRAM)，DDR 尚未初始化

| 步骤 | 操作 |
|---|---|
| 读取 MSEL | 从 `MSEL[3:0]` 引脚确定启动源 |
| 加载 SPL | 从 SPI Flash 或 SD 卡加载 SPL 到 `0x08000000` (L2 LIM) |
| 传递 DTB | 将设备树指针传入 `a1` 寄存器 |
| 跳转 | 跳转到 `0x08000000` 执行 SPL |

**MSEL 关键值：**

| MSEL[3:0] | 启动源 |
|---|---|
| `1011` | microSD 卡 |
| `0110` | QSPI0 x4 SPI Flash |
| `0000` | 等待调试器 (JTAG) |

**重要约束：** ZSBL 运行时 DDR 不可用。所有代码和数据必须在 on-chip SRAM 内。

## 阶段 2：U-Boot SPL — Secondary Program Loader

**运行位置：** L2 LIM (`0x08000000`)
**运行模式：** M-mode
**源码：** `src/u-boot/board/sifive/unmatched/spl.c`, `arch/riscv/cpu/fu740/spl.c`

### 2.1 入口 → DDR 就绪

```text
_start (arch/riscv/cpu/start.S)
  ├─ harts_early_init()           ← 启用分支预测、缓存 (CSR 0x7c1)
  ├─ board_init_f()               ← DM 驱动模型、串口
  │    └─ spl_board_init_f()      ← ⬅ 板级初始化核心
  │         ├─ spl_dram_init()    ← ① DDR 初始化（必须先做）
  │         ├─ PWM 初始化          ← ② LED (黄=红+绿) + 风扇
  │         ├─ GbE PHY 复位        ← ③ GPIO12 → VSC8541
  │         ├─ USB/PCIe 桥复位      ← ④ GPIO7  → ASM1042A
  │         ├─ USB Hub 复位        ← ⑤ GPIO11 → ASM1074
  │         └─ USB ULPI PHY 复位   ← ⑥ GPIO9  → USB3320C
  ├─ spl_relocate_stack_gd()      ← 栈和 GD 移至 DDR
  └─ board_init_r()               ← SPL 通用框架
```

### 2.2 board_init_r → 加载下一阶段

```text
board_init_r() (common/spl/spl.c)
  ├─ spl_boot_device()            ← 读取 MSEL 确定 SPI 还是 SD
  ├─ boot_from_devices()          ← 从启动介质加载 FIT
  │    └─ spl_load_simple_fit()   ← 解析 FIT 镜像
  │         ├─ 加载 firmware:   fw_dynamic.o → SPL_OPENSBI_LOAD_ADDR
  │         ├─ 加载 loadables:  u-boot.bin   → 对应地址
  │         ├─ 加载 fdt:        dtb          → spl_image->fdt_addr
  │         └─ 若 os == IH_OS_OPENSBI → jumper = spl_invoke_opensbi
  └─ spl_invoke_opensbi()         ← 准备 fw_dynamic_info 并跳转
```

### 2.3 SPL 内存布局

| 地址 | 用途 |
|---|---|
| `0x08000000` | SPL 代码段基址 (L2 LIM) |
| `0x081cfe60` | SPL 栈顶 |
| `0x100000` (1MB) | SPL 最大尺寸 |
| `0x80000000` | DDR 起始地址 |
| `0x80200000` | U-Boot proper 入口 (0x80000000 + 2MB) |
| `0x84000000` | FIT 镜像加载缓冲 (DDR) |
| `0x85000000` | SPL BSS 段 (DDR) |

**hart lottery 多核同步：** Boot hart 通过原子 `amoswap` 抢锁 (`hart_lottery`)，从 harts 在 `secondary_hart_loop` 中 WFI 等待 IPI。SPL 结束时 `smp_call_function(wait=1)` 确保所有从 hart 同步进入 OpenSBI。

## 阶段 3：OpenSBI — M-mode 运行时固件

**运行位置：** DRAM（SPL 加载）
**运行模式：** M-mode
**源码：** `src/opensbi/platform/generic/sifive/fu740.c`

### 3.1 fw_dynamic 启动协议

SPL 通过 `fw_dynamic_info` 结构体传递控制权：

```c
struct fw_dynamic_info opensbi_info = {
    .magic     = 0x4942534,       // "SBI" + version
    .version   = 2,               // FW_DYNAMIC_INFO_VERSION
    .next_addr = os_entry,        // U-Boot proper 入口 (= 0x80200000)
    .next_mode = 1,               // PRV_S (S-mode)
    .options   = SPL_SCRATCH_OPTIONS,
    .boot_hart = gd->arch.boot_hart
};
```

### 3.2 OpenSBI 在 Unmatched 上的职责

| 职责 | 说明 |
|---|---|
| SBI 运行时服务 | 定时器、IPI、控制台输出 (`earlycon=sbi`)、系统复位 |
| FU740 勘误 | `fu740.c` 中的平台特定的 errata 处理 |
| 电源管理 | PMIC 支持的 reset/shutdown |
| 模式切换 | 从 M-mode 切换到 S-mode，启动 `next_addr` 处代码 |
| 中断委托 | 将定时器和外部中断委托给 S-mode |

**OpenSBI 不负责：** DDR 初始化、PCIe 初始化、串口初始化、时钟初始化。
这些都是 SPL 或后续阶段的工作。

## 阶段 4：U-Boot Proper — 通用引导加载器

**运行位置：** DRAM (`0x80200000`)
**运行模式：** S-mode
**源码：** `src/u-boot/`

### 4.1 初始化到 distro boot

```text
U-Boot proper 入口 (S-mode)
  ├─ PCIe 枚举
  │    ├─ pcie_sifive_probe()          ← FU740 DWC 驱动
  │    ├─ pcie_sifive_init_phy()       ← CR_PARA 配置 8 lanes AC 终端
  │    ├─ pcie_sifive_force_gen1()     ← 强制 PCIe 1.0 (2.5 GT/s)
  │    ├─ 使能 LTSSM                    ← mgmt 寄存器
  │    ├─ 等待 link-up                  ← 轮询 DBI debug 寄存器
  │    └─ 配置 iATU outbound window     ← CPU 访问 PCIe 空间
  ├─ USB 初始化 (xhci_pci)
  │    └─ 扫描 USB 设备 (3 设备, 0 存储)
  ├─ MMC/SD 卡分区扫描
  └─ distro boot → /extlinux/extlinux.conf
```

### 4.2 extlinux → 内核重定位

```text
extlinux.conf 内容:
  label OpenEmbedded-SiFive-HiFive-Unmatched
      kernel /Image.gz
      fdt /hifive-unmatched-a00.dtb
      append root=/dev/mmcblk0p4 rootfstype=ext4 rootwait \
             console=ttySIF0,115200 earlycon=sbi

执行流程:
  ① 加载 Image.gz          → 0x84000000 (kernel_addr_r)
  ② 加载 DTB               → 0x88000000 (fdt_addr_r)
  ③ 解压 Image.gz          → 临时 buffer
  ④ memmove 回 0x84000000  → 读取 RISC-V Image Header
  ⑤ 读取 text_offset       → 0x200000 (2MB)
  ⑥ 计算目标地址           → 0x80000000 + 0x200000 = 0x80200000
  ⑦ memmove 到最终地址     → "Moving Image from 0x84000000 to 0x80200000"
  ⑧ DTB fixup              → /memory、MAC、bootargs、/chosen
  ⑨ 跳转内核               → a0=boot_hart, a1=0x88000000
```

**为什么必须重定位：**
1. Image.gz 是 gzip 压缩的，Image Header 中的 `text_offset` 要解压后才能读
2. 加载地址 `0x84000000` 与内核要求的 `0x80200000` 相差 62MB
3. 使用 `memmove` 而非 `memcpy` 是保守做法（虽然此处源和目的不重叠）

### 4.3 PCIe 枚举关键发现

| 发现 | 解释 |
|---|---|
| `PCIe Link up, Gen1` | ASM1042A 桥最高支持 Gen2，实际协商 Gen1 |
| `Device 0: unknown device` | U-Boot 设备 ID 库有限，不影响功能 |
| `3 USB Device(s) found` | ASM1042A xHCI → ASM1074 Hub → 下游设备 |
| `0 Storage Device(s) found` | 无 USB 大容量存储，不从 USB 启动 |

## 阶段 5：Linux 内核 → BusyBox

**运行位置：** DRAM (`0x80200000`)
**运行模式：** S-mode → 用户空间
**源码：** `src/linux/`

### 5.1 内核入口与早期初始化

```text
_start_kernel (arch/riscv/kernel/head.S)
  ├─ 计算重定位偏移 (运行时地址 vs 链接地址)
  ├─ 设置 early 页表 (Sv39, 2MB 大页对齐)
  ├─ 启用 MMU → 切换到虚拟地址
  ├─ setup_arch()
  │    ├─ 解析 DTB (设备树)
  │    ├─ 初始化 PLIC 中断控制器
  │    ├─ 初始化 CLINT 定时器
  │    ├─ earlycon=sbi → OpenSBI 字符输出
  │    └─ 探测平台设备 (从 DTS)
  ├─ PCIe 控制器初始化
  │    ├─ fu740_pcie_probe()           ← FU740 DWC 驱动
  │    ├─ dw_pcie_host_init()          ← DWC 通用 host 初始化
  │    │    ├─ fu740_pcie_host_init()  ← GPIO 复位序列
  │    │    ├─ dw_pcie_setup_rc()      ← 配置 Root Complex
  │    │    ├─ fu740_pcie_start_link() ← 强制 Gen1 → 训练 → 恢复原生速度
  │    │    └─ pci_host_probe()        ← 枚举设备 + 驱动绑定
  │    └─ NVMe/GPU/网卡 等端点驱动加载
  ├─ 挂载 rootfs (ext4, /dev/mmcblk0p4)
  └─ 执行 /sbin/init → /etc/init.d/rcS → shell
```

### 5.2 U-Boot vs Linux PCIe 初始化对比

| 方面 | U-Boot | Linux |
|---|---|---|
| 独立初始化 | 是，不依赖前阶段结果 | 是，不依赖 U-Boot 的枚举 |
| Gen1 策略 | 永久强制 Gen1 | Gen1 初始启动 → 重新协商到原生速度 |
| iATU | 单个 outbound MEM window | 多个动态区间 |
| MSI | 最小支持 | 完整 MSI/MSI-X + IRQ domain |
| 枚举范围 | 基本 BDF 扫描 + BAR 分配 | 完整枚举 + AER/ASPM/热插拔 |
| DT 来源 | U-Boot 内置 DTB | 独立 DTB（可能经 U-Boot fixup） |

### 5.3 最终内存布局（Linux 启动前）

```text
0x80000000 ─┬──────────────────────
            │  (2MB 保留空间 — early 页表)
0x80200000   ├─ Linux Image (≈14.7 MB)    ← a0 = boot_hart
            │
0x810B2000   ├─ Image 结束
            │
0x84000000   ├─ (曾加载 Image.gz，已可重用)
            │
0x88000000   ├─ DTB (≈22 KB fixup 后)      ← a1
0x880058E8   ├─ DTB 结束
            │
            │  (剩余 ~2GB DDR 可用)
            │
0xFFFFFFDF  ─┴──────────────────────
```

## FIT 镜像结构（SPI Flash 启动）

SPL 从 SPI Flash 加载的是 FIT 格式镜像：

```text
FIT Image (.itb)
  ├── /images
  │   ├── fw_dynamic.o     ← OpenSBI (firmware, 加载到 SPL_OPENSBI_LOAD_ADDR)
  │   ├── u-boot.bin        ← U-Boot proper (loadable)
  │   └── dtb               ← 设备树 (fdt)
  └── /configurations
      └── config-1
           firmware  = "fw_dynamic.o"
           loadables = "u-boot.bin"
           fdt       = "dtb"
```

**SPI Flash 典型布局：**

```text
0x00000000: U-Boot SPL         ← ZSBL 从这里加载
0x00010000: U-Boot env 环境变量
0x00100000: FIT Image (.itb)   ← SPL 从这里加载
```

## SD 卡 GPT 分区布局

```text
SD Card
├─ p1: SPL raw 区域 (无文件系统)
├─ p2: U-Boot ITB (raw, 无文件系统)
├─ p3: boot 分区 (FAT32/ext4, kernel + DTB + extlinux.conf)
└─ p4: rootfs (ext4, BusyBox)
```

**为什么 SPL 和 U-Boot ITB 使用 raw 分区而不是文件系统上的文件？**
因为 ZSBL 和 SPL 在文件系统驱动加载之前运行，它们只能从固定偏移量读取 raw 数据。

## 所有权总结

| 阶段 | 模式 | 主要职责 |
|---|---|---|
| ZSBL (ROM) | M-mode | 读取 MSEL，加载 SPL |
| U-Boot SPL | M-mode | DDR 初始化、外设复位、加载 FIT (OpenSBI + U-Boot + DTB) |
| OpenSBI | M-mode | SBI 运行时、模式切换、系统复位 |
| U-Boot proper | S-mode | PCIe/USB 枚举、distro boot、加载内核 |
| Linux | S-mode | PCIe 正式初始化、驱动绑定、用户空间 |

## 相关文档

- [U-Boot SPL 代码解析](spl-analysis.md) — SPL 从 `_start` 到 `spl_invoke_opensbi` 的完整代码追踪
- [U-Boot 启动日志分析](uboot-boot-log.md) — U-Boot proper 阶段：PCIe/USB 枚举、内核解压与重定位
- [PCIe 学习路线](../pcie/pcie-learning.md) — 五阶段 PCIe 学习计划
- [PCIe 学习笔记](../pcie/pcie-study.md) — 板级详情、DDR 初始化、JTAG 调试
- [内存布局参考](../reference/memory-map.md) — 全部物理地址速查
