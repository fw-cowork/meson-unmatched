# OpenSBI on HiFive Unmatched

本文档聚焦 OpenSBI 在 HiFive Unmatched 平台上的角色、实现和集成方式。
OpenSBI 在此平台上的职责有限但关键：它是 M-mode 运行时，不是 PCIe、
DDR 或外设的初始化者。

## 定位

```text
谁初始化 DDR?    → U-Boot SPL
谁初始化 PCIe?   → U-Boot proper (早期枚举) / Linux (正式初始化)
谁初始化串口?    → U-Boot SPL (preloader_console_init)
OpenSBI 做什么?  → M-mode SBI 服务、模式切换、系统复位、平台勘误

OpenSBI 不初始化硬件。它接收已经配好的硬件，提供运行时服务。
```

## 在启动链中的位置

```text
ZSBL (M-mode, ROM)
  → U-Boot SPL (M-mode, SRAM)
    → OpenSBI (M-mode, DRAM)           ← 本文档
      → U-Boot proper (S-mode, DRAM)
        → Linux (S-mode, DRAM)
```

OpenSBI 由 SPL 以 FW_DYNAMIC 模式加载。SPL 负责：
1. 初始化 DDR
2. 加载 `fw_dynamic.o` 到 DRAM
3. 准备 `fw_dynamic_info` 结构体
4. 跳转到 OpenSBI 入口

## FW_DYNAMIC 启动协议

### 为什么是 FW_DYNAMIC

OpenSBI 有三种加载模式：

| 模式 | 说明 | 适用场景 |
|---|---|---|
| FW_PAYLOAD | OpenSBI 内含 payload (如 U-Boot)，当作单一 blob 加载 | 简单嵌入式系统 |
| FW_JUMP | 固定地址跳转，无动态信息传递 | 早期固件 |
| **FW_DYNAMIC** | 接收前级传递的 `fw_dynamic_info`，动态获取下一级入口 | **Unmatched 使用此模式** |

FW_DYNAMIC 的优势：SPL 可以在运行时决定 U-Boot proper 的入口地址和 DTB 位置，
而不是在编译 OpenSBI 时硬编码。

### fw_dynamic_info 结构体

```c
struct fw_dynamic_info {
    unsigned long magic;      // 0x4942534 ("SBI" 魔数)
    unsigned long version;    // 2 (FW_DYNAMIC_INFO_VERSION)
    unsigned long next_addr;  // U-Boot proper 入口 (0x80200000)
    unsigned long next_mode;  // 1 = PRV_S (S-mode)
    unsigned long options;    // SPL 预置选项
    unsigned long boot_hart;  // Boot hart ID
};
```

**传递方式：** SPL 将上述结构体写入 DDR (`a2` 指向它)，调用 OpenSBI 入口。
OpenSBI 读取后，在 S-mode 启动 `next_addr` 处的代码。

### SPL 侧的调用

代码位于 `common/spl/spl_opensbi.c`：

```text
spl_invoke_opensbi(spl_image)
  ├─ 验证 fdt_addr 非空且 8 字节对齐
  ├─ 从 FIT 中定位 U-Boot proper 节点 (os="u-boot")
  ├─ 读取 entry 地址 → os_entry
  ├─ 构造 fw_dynamic_info:
  │    .magic     = 0x4942534
  │    .version   = 2
  │    .next_addr = os_entry          ← OpenSBI 完成后跳转目标
  │    .next_mode = 1                 ← PRV_S
  │    .boot_hart = gd->arch.boot_hart
  ├─ [SMP] smp_call_function(entry, dtb, &info, wait=1)
  │    向所有从 hart 发送 IPI，等待确认
  │    防止 OpenSBI 重定位时还在 SPL 代码区执行的冲突
  └─ opensbi_entry(boot_hart, dtb_addr, &info)
```

## OpenSBI 的运行时职责

### SBI 服务

| SBI 调用 | 用户 | 用途 |
|---|---|---|
| `sbi_console_putchar` | Linux earlycon | 早期串口输出 (`earlycon=sbi`) |
| `sbi_set_timer` | S-mode OS | 设置定时器中断 |
| `sbi_send_ipi` | S-mode OS | 发送核间中断 |
| `sbi_system_reset` | S-mode OS | 系统复位/关机 |

### 模式管理与中断委托

OpenSBI 的核心功能是代表 S-mode 管理 M-mode 特权资源：

```text
中断委托 (mideleg/medeleg):
  - 定时器中断  → 委托给 S-mode
  - 外部中断    → 委托给 S-mode (PLIC)
  - 环境调用    → 在 M-mode 处理 (ecall from S → M)

CSR 管理:
  - stvec  (S-mode 异常向量)
  - scause (S-mode 异常原因)
  - stval  (S-mode 异常值)
  - sepc   (S-mode 异常 PC)
  这些 CSR 由 S-mode OS 管理，OpenSBI 不干涉。
```

### 平台特定功能 (fu740.c)

OpenSBI 的 FU740 平台文件 (`platform/generic/sifive/fu740.c`) 负责：

| 功能 | 实现方式 |
|---|---|
| 系统复位 | 通过 PMIC (电源管理 IC) 执行硬件复位 |
| 系统关机 | 通过 PMIC 切断电源 |
| 平台勘误 | FU740 特定的 CSR 修复 |

**不包含的内容：**
- DDR 控制器初始化 → 由 SPL 完成
- PCIe/SerDes 初始化 → 由 U-Boot proper / Linux 完成
- 串口初始化 → 由 SPL 完成 (`NS16550` 兼容 UART)
- 时钟树设置 → 由 SPL 完成 (`PRCI`)

## 内存与地址

### OpenSBI 在 DDR 中的位置

```text
OpenSBI 由 SPL 加载到: SPL_OPENSBI_LOAD_ADDR = 0x80000000
OpenSBI 重定位后:      FW_TEXT_START 配置的值

Unmatched 上:
  FW_TEXT_START = 0x80000000 (DDR 起始)
```

这意味着 OpenSBI 占据 DDR 的最低地址区域，紧挨着 U-Boot proper 的
入口 `0x80200000`。2MB 的间隔被 OpenSBI 代码段和 Linux `text_offset`
共享使用。

### MMIO 区域与 OpenSBI

OpenSBI 不编程以下 MMIO 区域，但通过 domain 机制可以限制 S-mode
对它们的访问：

| 区域 | 访问策略 |
|---|---|
| PLIC (`0x0c000000`) | S-mode 可访问（中断处理） |
| CLINT (`0x10000000`) | M-mode 管理，S-mode 通过 SBI 调用使用 |
| UART (`0x10010000`) | S-mode 可访问（串口驱动） |
| PCIe DBI/Config (`0xe0000000`, `0xdf000000`) | S-mode 可访问 |

## QEMU 上的 OpenSBI

QEMU virt 平台使用 OpenSBI 的方式与物理板不同：

| 方面 | 物理板 | QEMU |
|---|---|---|
| 加载方式 | SPL 从 SPI/SD 加载 fw_dynamic | QEMU `-bios fw_dynamic.elf` 直接加载 |
| 启动模式 | FW_DYNAMIC (前级 SPL) | FW_DYNAMIC (前级 QEMU) |
| 硬件初始化 | 无（SPL 做） | 无（QEMU 虚拟机） |
| 下一级 | U-Boot proper (S-mode) | QEMU S-mode U-Boot |

## 调试 OpenSBI

### QEMU GDB 调试

```bash
# 终端 1: 启动 QEMU GDB server (暂停在复位处)
./qemu-gdb.sh --build

# 终端 2: 连接 GDB
gdb-multiarch deploy/qemu/fw_dynamic.elf
(gdb) target remote 127.0.0.1:1234
(gdb) b fw_platform_init        # OpenSBI 平台初始化断点
(gdb) continue
```

### 物理板 JTAG 调试

物理板上通过 FTDI JTAG 调试 OpenSBI：

```bash
# 启动 OpenOCD
openocd -f openocd_hifive_unmatched.cfg

# GDB 连接
gdb-multiarch deploy/fw_dynamic.elf
(gdb) target extended-remote :3333
(gdb) monitor reset halt
(gdb) b fw_platform_init
(gdb) continue
```

若需在 OpenSBI 最早入口处中断，将 `MSEL` 设为 `0000`（调试器等待模式），
然后通过 JTAG 加载并单步执行。

## 常见误区

| 误区 | 事实 |
|---|---|
| "OpenSBI 初始化 PCIe" | OpenSBI 不触碰 PCIe 寄存器。PCIe 初始化在 U-Boot proper 和 Linux 中 |
| "OpenSBI 训练 DDR" | DDR 训练由 U-Boot SPL 完成。OpenSBI 运行时 DDR 已经可用 |
| "OpenSBI 是 Linux 内核的一部分" | OpenSBI 是独立的 M-mode 固件，Linux 通过 ecall 调用 SBI 服务 |
| "OpenSBI 配置内核启动参数" | 启动参数 (bootargs) 由 U-Boot 通过 extlinux.conf 或手动设置 |
| "fw_dynamic 模式不需要 SPL" | fw_dynamic 正是由前一阶段 (SPL) 加载和配置的 |

## 相关文档

- [启动链总览](boot-chain-overview.md) — OpenSBI 在整个启动链中的位置
- [U-Boot SPL 代码解析](spl-analysis.md) §5 — `spl_invoke_opensbi` 调用流程
- [PCIe 学习笔记](../pcie/pcie-study.md) §4 — OpenSBI 与 PCIe 的关系
- [Framework 设计文档](../architecture/DESIGN.md) §13 — OpenSBI 构建管道
