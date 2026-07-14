# HiFive Unmatched U-Boot 启动日志分析

分析 U-Boot proper 从接管硬件到启动 Linux 内核的完整过程。

对应的启动日志：

```
PCIe Link up, Gen1

Device 0: unknown device
starting USB...
Bus xhci_pci: Register 4000840 NbrPorts 4
Starting the controller
USB XHCI 1.00
scanning bus xhci_pci for devices... 3 USB Device(s) found
       scanning usb for storage devices... 0 Storage Device(s) found

Device 0: unknown device
switch to partitions #0, OK
mmc0 is current device
Scanning mmc 0:3...
Found /extlinux/extlinux.conf
Retrieving file: /extlinux/extlinux.conf
204 bytes read in 9 ms (21.5 KiB/s)
1:      OpenEmbedded-SiFive-HiFive-Unmatched
Retrieving file: /Image.gz
7247925 bytes read in 4731 ms (1.5 MiB/s)
append: root=/dev/mmcblk0p4 rootfstype=ext4 rootwait console=ttySIF0,115200 earlycon=sbi
Retrieving file: /hifive-unmatched-a00.dtb
10473 bytes read in 13 ms (786.1 KiB/s)
   Uncompressing Kernel Image
Moving Image from 0x84000000 to 0x80200000, end=810b2000
## Flattened Device Tree blob at 88000000
   Booting using the fdt blob at 0x88000000
   Using Device Tree in place at 0000000088000000, end 00000000880058e8
```

---

## 目录

1. [PCIe 初始化](#1-pcie-初始化)
2. [USB 初始化](#2-usb-初始化)
3. [MMC/SD 卡扫描与 distro boot](#3-mmcsd-卡扫描与-distro-boot)
4. [extlinux 引导协议](#4-extlinux-引导协议)
5. [内核与设备树加载](#5-内核与设备树加载)
6. [内核解压与重定位（重点）](#6-内核解压与重定位)
7. [FDT 传递与跳转内核](#7-fdt-传递与跳转内核)

---

## 1. PCIe 初始化

```
PCIe Link up, Gen1
Device 0: unknown device
```

U-Boot proper 在 `board_init_r` 阶段执行 PCIe 总线枚举（`CONFIG_PCI=y`, `CONFIG_PCIE_DW_SIFIVE=y`）。

**硬件背景：**
- FU740 集成 Synopsys DesignWare PCIe 控制器（`PCIE_DW_SIFIVE` 驱动）
- 板上通过 PCIe 连接 ASM1042A USB 3.0 xHCI 桥接芯片

**Gen1 链路速率：**
- 协商到 PCIe 1.0 (2.5 GT/s)，而非 FU740 支持的 Gen3 (8 GT/s)
- **原因：** ASM1042A 桥接芯片最高只支持 PCIe Gen2，且可能在 Gen1 完成链路训练。FU740 下行兼容 Gen1
- SPL 阶段 `spl_usb_pcie_bridge_init()` 已经通过 GPIO7 对这颗 ASM1042A 做了硬件复位

**"unknown device" 的含义：**
- U-Boot 的 PCIe 枚举只做基本扫描，不加载完整驱动
- 设备 ID 不在 U-Boot 已知列表中即显示 "unknown"
- 这并不影响功能 — xHCI 驱动通过 class code 匹配，不依赖 device ID

---

## 2. USB 初始化

```
starting USB...
Bus xhci_pci: Register 4000840 NbrPorts 4
Starting the controller
USB XHCI 1.00
scanning bus xhci_pci for devices... 3 USB Device(s) found
       scanning usb for storage devices... 0 Storage Device(s) found
```

**初始化流程：**

1. U-Boot 通过 PCI 总线探测到 xHCI 控制器（class code 0x0c0330）
2. `Register 4000840` — xHCI MMIO 寄存器基址，由 PCIe BAR0 映射
3. `NbrPorts 4` — ASM1042A 提供 4 个 USB 3.0 下行端口
4. **3 USB Device(s) found** — 检测到 3 个 USB 设备：
   - ASM1074 USB Hub（SPL 中 `spl_usb_hub_init` 通过 GPIO11 复位过）
   - Hub 下挂的设备（键盘、鼠标等）
5. **0 Storage Device(s)** — 没有 USB 大容量存储设备，不会尝试从 USB 启动

**涉及的 Kconfig：**
```
CONFIG_USB=y
CONFIG_USB_XHCI_HCD=y
CONFIG_USB_XHCI_PCI=y
```

---

## 3. MMC/SD 卡扫描与 distro boot

```
Device 0: unknown device
switch to partitions #0, OK
mmc0 is current device
Scanning mmc 0:3...
```

- `mmc0` 是 SD 卡控制器
- `switch to partitions #0` — 切换到主分区表模式（GPT 分区表）
- `Scanning mmc 0:3` — U-Boot 的 **distro boot** 机制按固定顺序扫描 boot 分区

**distro boot 扫描顺序：**
1. 扫描所有块设备的第 1 分区（FAT/EXT）
2. 寻找 `/extlinux/extlinux.conf` 或 `/boot/extlinux/extlinux.conf`
3. 如果找到，按配置启动；否则尝试 EFI boot

**典型 Unmatched GPT 分区布局：**

```
SD Card / SPI Flash
├─ p1: SPL + U-Boot (raw, 无文件系统)
├─ p2: U-Boot ITB / FIT 镜像
├─ p3: boot 分区 (FAT32/ext4, 含 kernel + extlinux)
└─ p4: rootfs (ext4)
```

---

## 4. extlinux 引导协议

```
Found /extlinux/extlinux.conf
Retrieving file: /extlinux/extlinux.conf
204 bytes read in 9 ms (21.5 KiB/s)
1:      OpenEmbedded-SiFive-HiFive-Unmatched
```

`extlinux.conf` 是 **U-Boot distro boot（通用发行版引导）** 使用的标准配置文件格式，源自 syslinux/extlinux 项目。

**相关 Kconfig：**
```
CONFIG_BOOTSTD_DEFAULTS=y   → 启用标准 distro boot 流程
CONFIG_BOOTSTD_BOOTCOMMAND=y → bootcmd 使用 bootstd 扫描
```

**extlinux.conf 典型内容：**

```
label OpenEmbedded-SiFive-HiFive-Unmatched
    kernel /Image.gz
    fdt /hifive-unmatched-a00.dtb
    append root=/dev/mmcblk0p4 rootfstype=ext4 rootwait console=ttySIF0,115200 earlycon=sbi
```

四个关键字段：

| 字段 | 含义 |
|---|---|
| `label` | 启动项显示名称 |
| `kernel` | 内核镜像路径（相对于 boot 分区） |
| `fdt` | 设备树文件路径 |
| `append` | 内核命令行参数 |

U-Boot 解析 extlinux.conf 后，会依次执行：
1. 加载 kernel 文件到 `kernel_addr_r` 地址
2. 加载 fdt 文件到 `fdt_addr_r` 地址
3. 设置 bootargs 环境变量为 append 的值
4. 调用 `booti` 命令启动内核

> **注意：** 标签 `OpenEmbedded-SiFive-HiFive-Unmatched` 表明这个内核镜像由 **Yocto/OE meta-sifive** 构建。

---

## 5. 内核与设备树加载

```
Retrieving file: /Image.gz
7247925 bytes read in 4731 ms (1.5 MiB/s)
Retrieving file: /hifive-unmatched-a00.dtb
10473 bytes read in 13 ms (786.1 KiB/s)
```

### 5.1 Image.gz

- **格式：** gzip 压缩的 Linux 内核 Image（不是 vmlinux ELF，不是 uImage）
- **大小：** ~7.25 MB（压缩后）/ ~14.7 MB（解压后，由后面 `end=810b2000` 算出）
- **加载地址：** `kernel_addr_r`，通常设为 `0x84000000`（与 SPL 的 `CONFIG_SPL_LOAD_FIT_ADDRESS` 一致）
- **读取速度 1.5 MiB/s** — 偏低，说明内核可能存放在 SPI Flash 而非 SD 卡（SD 卡应在 20+ MiB/s）

### 5.2 hifive-unmatched-a00.dtb

- **格式：** 编译后的 Device Tree Blob
- **大小：** ~10 KB
- **加载地址：** `fdt_addr_r`，此处为 `0x88000000`
- **读取速度 ~786 KiB/s** — 数值看起来高是因为文件很小（仅 10KB），开销主要来自文件系统寻址延迟

### 5.3 内核命令行

```
append: root=/dev/mmcblk0p4 rootfstype=ext4 rootwait console=ttySIF0,115200 earlycon=sbi
```

| 参数 | 含义 |
|---|---|
| `root=/dev/mmcblk0p4` | 根文件系统在 SD 卡第 4 分区 |
| `rootfstype=ext4` | 根文件系统类型 |
| `rootwait` | 等待根设备就绪后再挂载 |
| `console=ttySIF0,115200` | 串口控制台：SiFive UART0，波特率 115200 |
| `earlycon=sbi` | 早期控制台通过 SBI 输出（OpenSBI 提供的字符输出服务） |

---

## 6. 内核解压与重定位

```
   Uncompressing Kernel Image
Moving Image from 0x84000000 to 0x80200000, end=810b2000
```

这是整个启动过程中最关键的内存操作，涉及两次数据搬运。下面逐步分析。

### 6.1 涉及的地址

```
压缩加载地址:        0x84000000  (kernel_addr_r)
解压临时缓冲:         kernel_comp_addr_r (环境变量指定)
解压后暂存:           0x84000000  (memmove 回原地址)
最终内核入口:        0x80200000  (0x80000000 + text_offset)

内核大小:            0x810b2000 - 0x80200000 = 0xE92000 ≈ 14.7 MB

DTB 地址:            0x88000000
```

### 6.2 关键数据结构：RISC-V Image Header

Linux 内核 Image 的前 64 字节是标准化的 RISC-V Image Header（定义在 `arch/riscv/include/asm/image.h`）：

```c
struct riscv_image_header {
    u32 code0;          // offset 0:  可执行代码（RISC-V 指令）
    u32 code1;          // offset 4:  可执行代码
    u64 text_offset;    // offset 8:  ⬅ 内核期望的加载偏移（0x200000）
    u64 image_size;     // offset 16: Image 总大小（包含 BSS），0 表示未知
    u64 flags;          // offset 24: 内核标志位
    u32 version;        // offset 32: 头部版本号
    u32 res1;           // offset 36: 保留
    u64 res2;           // offset 40: 保留
    u64 magic;          // offset 48: "RISCV\0\0\0" (已弃用)
    u32 magic2;         // offset 56: "RSC\x05" (与 ARM64 Image 对齐)
    u32 res3;           // offset 60: 保留
};
```

**`text_offset = 0x200000` (2MB)** 的含义：

- Linux 内核构建时，默认 `text_offset` 是 2MB
- 这是从 RAM 起始地址（`0x80000000`）到内核代码段起始的偏移
- 2MB 对齐是因为 RISC-V Sv39 页表要求 2MB 大页对齐
- 余出的 2MB 空间用于放置 early 页表等启动时数据结构

**为什么 text_offset 必须在 Header 中声明：**

U-Boot 是通用的 bootloader，不能硬编码某个特定平台的地址。通过读取 Image Header 中的 `text_offset`，同一个 U-Boot 二进制可以：
- 在 Unmatched 上加载内核到 `0x80200000`（RAM 从 0x80000000 开始）
- 在其他 RISC-V 平台上加载到不同的地址（RAM 从不同基址开始）

### 6.3 完整的三段式加载流程

这是 `cmd/booti.c` 中 `booti_start()` 的实现逻辑：

```
                      Image.gz (7.25 MB)
                            │
                            ▼
              ┌─────────────────────────────┐
              │  ① gzip 解压                 │
              │  image_decomp()              │
              │  输入: ld = 0x84000000        │
              │  输出: kernel_comp_addr_r    │  (临时 buffer)
              │  解压后大小: dest_end 字节    │
              └──────────────┬──────────────┘
                             │
              ┌──────────────▼──────────────┐
              │  ①b memmove 回原址            │
              │  memmove(0x84000000,         │
              │          kernel_comp_addr_r, │
              │          dest_end)           │
              │  → 未压缩的 Image 现在在      │
              │    0x84000000                │
              └──────────────┬──────────────┘
                             │
              ┌──────────────▼──────────────┐
              │  ② 读取 Image Header         │
              │  booti_setup(0x84000000,     │
              │    &relocated_addr,          │
              │    &image_size, false)       │
              │                              │
              │  读取 Header 中:              │
              │    text_offset  = 0x200000   │
              │    image_size   = 0xE92000   │
              │                              │
              │  计算目标地址:                │
              │  relocated_addr              │
              │    = image_load_addr         │
              │      + text_offset           │
              │    = 0x80000000 + 0x200000   │
              │    = 0x80200000              │
              └──────────────┬──────────────┘
                             │
              ┌──────────────▼──────────────┐
              │  ③ memmove 到最终地址        │
              │  if (0x80200000 !=           │
              │      0x84000000):            │
              │    memmove(0x80200000,       │
              │            0x84000000,       │
              │            0xE92000)         │
              │                              │
              │  ⬆ 这就是日志中的            │
              │  "Moving Image from          │
              │   0x84000000 to 0x80200000"  │
              └──────────────────────────────┘
```

### 6.4 为什么必须重定位？

三个原因，层层递进：

**原因一：gzip 隐藏了 Image Header**

```
压缩前:  [Image Header (64B)] [Kernel Code (14.7MB)]
              ↓ gzip
压缩后:  [gzip stream (7.25MB)]   ← Header 不可直接读取
```

- Gzip 流式压缩包裹了整个 Image，包括 64 字节的 Header
- **`text_offset` 和 `image_size` 只有在解压完成后才能读取**
- 所以 U-Boot 不能提前知道目标地址，必须 "先解压，再决定放哪"

> 对比：如果是未压缩的 `Image` 文件，Header 可直接读取，U-Boot 可以在加载前就确定目标地址。

**原因二：加载地址与目标地址不同**

```
RAM 布局:
  0x80000000 ─┬─ RAM 起始（4GB DDR）
  0x80200000   ├─ 内核目标入口  ← text_offset = 2MB
              │   (14.7 MB Image)
  0x810b2000   ├─ 内核结束
              │
  0x84000000   ├─ kernel_addr_r  ← distro boot 加载地址
              │   (7.25 MB Image.gz)
  0x846EA875   ├─ gz 结束
              │
  0x88000000   ├─ fdt_addr_r (DTB)
```

- Distro boot 将文件加载到 `kernel_addr_r = 0x84000000`（通用 load buffer）
- Linux 内核要求位于 `0x80200000`（由 `text_offset` 决定）
- 两者的 64MB 偏移保证了**即使解压后也不会覆盖压缩源数据**

**原因三：memmove 而非 memcpy**

使用 `memmove` 而非 `memcpy` 是因为内核代码中普遍如此 — 当源和目标可能重叠时 `memmove` 是安全的。在此场景下虽然源和目标不重叠（`0x84000000` → `0x80200000`，向下移动 62MB），但 U-Boot 统一使用 `memmove` 以保证通用性：

```c
// cmd/booti.c:82-85
if (relocated_addr != ld) {
    printf("Moving Image from 0x%lx to 0x%lx, end=0x%lx\n", ld,
           relocated_addr, relocated_addr + image_size);
    memmove((void *)relocated_addr, (void *)ld, image_size);
}
```

### 6.5 内存重叠分析

```
解压后 Image 位于:  [0x84000000 ──────── 0x84E92000)  14.7 MB
               向下移动 62MB ↓
目标地址:           [0x80200000 ──────── 0x810B2000)  14.7 MB

0x84E92000 (源结束) < 0x88000000 (DTB)  ← 不覆盖 DTB
0x810B2000 (目标结束) < 0x84000000 (源起始) ← 源和目标完全不重叠
```

- 源区间：`0x84000000` ~ `0x84E92000`
- 目标区间：`0x80200000` ~ `0x810B2000`
- 两者相距 `0x84000000 - 0x810B2000 = 0x2F4E000` ≈ **47.3 MB**
- **完全无重叠**，`memcpy` 也是安全的，使用 `memmove` 是保守做法

### 6.6 为什么不直接解压到 0x80200000？

这是最自然的问题。技术上完全可以，但需要改变加载顺序：

1. 先把 Image.gz 加载到一个安全的临时位置（不与 `0x80200000` 重叠）
2. 读取 gzip header 获取解压后大小（gzip 末尾 4 字节存储原始大小）
3. 尝试直接解压到 `0x80200000`

但 U-Boot 选择 "解压 → 读 Header → 移动" 的三段式设计是出于：

- **通用性** — 不假设压缩格式在压缩流中存储了原始大小（并非所有格式都有）
- **安全性** — 不依赖 gzip trailer 的原始大小（可能被截断或损坏），而是依赖解压后的 Image Header，该 Header 在编译时由内核构建系统写入
- **一致性** — 压缩和非压缩 Image 走同一套 `booti_setup()` 代码路径

### 6.7 内核如何知道自己被搬到了正确地址

RISC-V Linux 内核支持 `CONFIG_RELOCATABLE`，可以在运行时重新定位。但即使不开启该选项，内核入口 `head.S` 会计算运行时地址与链接地址的差值，进行 self-relocation：

```asm
// arch/riscv/kernel/head.S (简化)
_start_kernel:
    // 计算重定位偏移
    la   t0, _start_kernel      // 链接地址（虚拟）
    // 获取当前 PC 物理地址
    // delta = physical_pc - link_addr
    // 使用 delta 修正所有绝对地址引用
```

这意味着：
1. `text_offset` 给出的是**建议**加载偏移，kernel 会据此做初始页表映射
2. 内核可以在一定程度上容忍不同的加载地址
3. 但 `text_offset` 的 2MB 对齐是**硬要求**（页表粒度的限制）

---

## 7. FDT 传递与跳转内核

```
## Flattened Device Tree blob at 88000000
   Booting using the fdt blob at 0x88000000
   Using Device Tree in place at 0000000088000000, end 00000000880058e8
```

### 7.1 FDT 地址与大小

- FDT 加载到 `0x88000000`（= `fdt_addr_r`）
- `end 0x880058e8` → DTB 大小 = `0x58e8` = **22,760 字节**

这个 DTB 比原始文件（10,473 字节）大了一倍多，因为 U-Boot 在启动前对 DTB 做了 **fixup**：
- 添加 `/memory` 节点（从 `gd->bd->bi_dram` 获取实际的 DRAM 大小）
- 添加 MAC 地址到网卡节点（从 EEPROM 读取）
- 设置 bootargs 属性（内核命令行）
- 添加 initrd 信息（如果有）
- 生成 `/chosen` 节点（stdout-path 等）

### 7.2 内核调用约定

RISC-V Linux 内核使用以下启动约定（与 ARM64 一致）：

```
a0 = boot hart ID  (gd->arch.boot_hart)
a1 = DTB 指针      (0x88000000)
```

U-Boot 代码（`arch/riscv/lib/bootm.c`）：

```c
static void boot_jump_linux(struct bootm_headers *images, int flag)
{
    void (*kernel)(ulong hart, void *dtb);
    kernel = (void (*)(ulong, void *))images->ep;   // ep = 0x80200000

    if (!fake) {
        kernel(gd->arch.boot_hart, images->ft_addr);  // 绝不返回
    }
}
```

### 7.3 完整内存布局（启动前最终状态）

```
0x80000000 ─┬──────────────────────────
            │  (2MB 保留空间 — early 页表)
0x80200000   ├─ Linux Image (14.7 MB)      ← images->ep
            │  ┌─────────────────
            │  │ text_offset       内核对齐要求 2MB
            │  │ image_size        0xE92000
0x810B2000   ├─ Image 结束
            │
            │  (约 47 MB 可用空间)
            │
0x84000000   ├─ (曾是 Image.gz 位置，现已可重用)
            │
            │  (约 64 MB)
            │
0x88000000   ├─ DTB (22,760 bytes)         ← a1 传递给内核
0x880058E8   ├─ DTB 结束
            │
            │  (剩余 ~2GB DDR)
            │
0xFFFFFFDF  ─┴──────────────────────────  (4GB RAM 结束附近)
```

### 7.4 启动时序总结

```
U-Boot proper (S-mode)
  │
  ├─ PCIe 扫描 → ASM1042A xHCI USB 桥 (Gen1)
  ├─ USB xHCI 初始化 → 3 设备, 0 存储设备
  ├─ MMC 分区扫描 → mmc 0:3
  ├─ extlinux.conf 解析 → label: OpenEmbedded-SiFive-HiFive-Unmatched
  │
  ├─ 加载 Image.gz 到 0x84000000 (7.25 MB)
  ├─ 加载 DTB 到 0x88000000 (10 KB → fixup 后 22 KB)
  │
  ├─ booti_start():
  │   ├─ ① gzip 解压 Image.gz → 临时 buffer
  │   ├─ ② memmove 回到 0x84000000
  │   ├─ ③ 读 Image Header: text_offset = 0x200000
  │   ├─ ④ 计算目标 = 0x80000000 + 0x200000 = 0x80200000
  │   └─ ⑤ memmove(0x80200000, 0x84000000, 14.7 MB)
  │
  └─ boot_jump_linux():
      kernel(a0=boot_hart, a1=0x88000000)
          ↓
      Linux 内核入口 (_start_kernel)
```

---

## 附录：与 SPL 启动阶段的对照

| 阶段 | 运行模式 | 主要工作 | 分析文档 |
|---|---|---|---|
| ZSBL | M-mode (ROM) | 加载 SPL | — |
| U-Boot SPL | M-mode | DDR + 外设初始化，加载 FIT | [spl-analysis.md](spl-analysis.md) |
| OpenSBI | M-mode | 平台抽象，S-mode 委托 | [spl-analysis.md](spl-analysis.md) §5 |
| U-Boot proper | S-mode | PCIe/USB 枚举，加载内核 | 本文档 |
| Linux | S-mode | 操作系统 | — |
