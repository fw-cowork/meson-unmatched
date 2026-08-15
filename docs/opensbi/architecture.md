# OpenSBI 架构：从特权级到运行时

本文回答三个问题：OpenSBI 为什么必须存在、它在 M-mode 中具体保护什么、以及源码
中的各个子系统如何组合成一个可复用的 SBI runtime。阅读时以本仓库固定的
OpenSBI commit `74434f255873d74e56cc50aa762d1caf24c099f8` 为准。

## 1. 先分清四个角色

| 角色 | 运行模式 | 主要责任 | 是否调用 SBI |
|---|---:|---|---|
| ZSBL/SPL | M | 复位后最早的软件、DDR、镜像加载、交接 | 通常不调用，负责进入 OpenSBI |
| OpenSBI | M | M-mode 固件、特权资源代理、跨 HART 协调 | 提供 SBI，不是 SBI 的调用者 |
| U-Boot proper | S | 启动策略、设备枚举、加载内核 | 是，使用 Timer/Console/HSM 等 |
| Linux | S | MMU、进程、驱动、文件系统 | 是，使用 Timer/IPI/RFENCE/SRST 等 |

这里的 M/S 是硬件特权级，不是“固件/应用”的软件分类。SPL 也可能是 M-mode，
而 U-Boot proper 也可以按某些配置运行在 M-mode；最终由前级传给 OpenSBI 的
`next_mode` 决定 `mret` 后进入哪一级。

## 2. 为什么 S-mode 需要 SBI

S-mode 不能直接可靠地完成以下工作：

1. 写 M-mode 专用 CSR，例如 `mstatus`、`mie`、`mtvec`、`pmp*`；
2. 操作 M-mode timer、跨 HART software interrupt、平台复位控制器；
3. 在多核上让其它 HART 执行 `fence.i`/`sfence.vma`；
4. 统一处理不同 SoC 的电源管理、外部中断控制器和计时器硬件差异。

SBI 把这些操作收敛为稳定的 ecall ABI。Linux/U-Boot 只依赖 SBI extension ID 和返回
错误码，不需要知道 FU740 的 PMIC I2C 地址或 CLINT/ACLINT 的寄存器布局。

```text
S-mode caller
    |  a7=extension, a6=function, a0-a5=args
    |  ecall
    v
M-mode trap entry (fw_base.S)
    v
lib/sbi/sbi_trap.c -> lib/sbi/sbi_ecall.c -> extension handler
    |  a0=error, a1=value, mepc += 4
    v
mret -> S-mode instruction after ecall
```

OpenSBI 是 SBI 的一个实现，而不是规范本身。规范由 RISC-V SBI 文档定义；实现可以
选择不注册某个 extension，调用者必须先用 Base `PROBE_EXT` 检查可用性。

## 3. 特权级、trap 和 delegation

一次 S-mode 异常到达 OpenSBI 的最小硬件过程是：

1. 硬件把原因写入 `mcause`，把附加地址/指令写入 `mtval`，把返回 PC 写入 `mepc`；
2. `mstatus.MPIE`/`MIE` 保存并关闭 M-mode 中断，硬件跳到 `mtvec`；
3. OpenSBI 汇编保存 GPR 和 CSR，C 层判断是中断还是异常；
4. C 层处理或把 trap 重新导向低特权级；
5. 恢复 context，`mret` 根据 `mstatus.MPP` 回到原来的模式。

`medeleg`/`mideleg` 可以把部分异常直接交给 S-mode 的 `stvec`，但 S-mode ecall
（`mcause=9`）通常保留给 M-mode，以便 SBI 工作。具体委托位由
`lib/sbi/sbi_hart.c` 根据硬件能力和平台策略设置，不能假设所有异常都委托。

### 3.1 关键 CSR 的学习顺序

```text
mtvec       M-mode trap 入口地址和模式
mepc        M-mode trap 返回地址
mcause      中断/异常原因
mtval       异常附加信息
mstatus     MIE/MPIE、MPP、FS/XS 等状态
mscratch    当前 HART 的 OpenSBI scratch 指针
medeleg     委托给 S/HS-mode 的异常位图
mideleg     委托给 S/HS-mode 的中断位图
pmpcfg/pmpaddr  M-mode 对下一级的物理内存访问边界
satp        S-mode 地址转换；OpenSBI 切换前会清理旧值
```

## 4. OpenSBI 的四层边界

### 4.1 firmware 层

`firmware/` 负责“如何进来、如何准备运行时、如何出去”，包括：

- 汇编入口、PIE 重定位和 BSS 清零；
- 临时栈、每 HART scratch/stack/heap；
- 从前级读取 FDT 和 `fw_dynamic_info`；
- 设置 `mtvec`，调用 `sbi_init()`；
- 通过 `mret` 把控制权交给 `next_addr/next_mode`。

它不应实现 UART、PMIC 或 Linux 启动策略。

### 4.2 lib/sbi 通用 runtime

`lib/sbi/` 是平台无关的 M-mode 服务：trap、ecall、HSM、timer、IPI、TLB、domain、
PMU、系统复位抽象和锁/堆/字符串。平台只提供设备操作或 capability，通用层负责
生命周期、权限检查和 SBI 返回值。

### 4.3 lib/utils 设备工具

`lib/utils/` 把常见硬件抽象为设备对象，例如 `sbi_timer_device`、`sbi_ipi_device`、
`sbi_system_reset_device`。FDT driver 根据 `compatible` 选择实现，避免在通用 SBI
handler 中写 SoC 型号判断。

### 4.4 platform 层

`struct sbi_platform` 是平台契约。平台可以实现 cold/warm/nascent/final 初始化、
HART 计数和映射、timer/ipi/irqchip/reset 入口以及 errata hook。`platform/generic`
通过 FDT 复用这些能力，再由 `sifive/fu740.c` 注入 FU740 特有行为。

## 5. 每 HART 的 scratch：OpenSBI 的线程本地存储

`struct sbi_scratch` 默认每个 HART 预留 4 KiB。`mscratch` 永远指向当前 HART 的
scratch；trap 入口通过交换 `tp`/`mscratch` 找到它。固定字段包括：

```text
+0x00  fw_start/fw_size/fw_rw_offset
+...   fw_heap_addr、next_arg1、next_addr、next_mode
+...   warmboot_addr、platform_addr、hartid_to_scratch
+...   trap_context、temporary、options、hartindex
+0x1000 结束（实际 offset 由 include/sbi/sbi_scratch.h 断言）
```

动态 offset 用 `sbi_scratch_alloc_offset()` 分配，并在每个 HART 的 scratch 中保存。
因此不能把一个普通全局变量当作“当前 HART 状态”；多核代码要么使用 scratch，要么
使用带锁的共享对象。共享 heap 由 coldboot 初始化，per-HART stack 从 scratch 后面的
布局计算。

## 6. Domain、PMP 与访问边界

OpenSBI 的 root domain 描述固件、内存、MMIO 和可见 HART。每个 memory region 同时
表达两组权限：M-mode 权限和 S/U-mode 权限，例如：

```text
firmware text   M:RX   S/U:---
firmware rw     M:RW   S/U:---
DDR payload     M:RX/RW  S/U:RX/RW
MMIO            M:RW   S/U:RW (按平台策略)
```

`sbi_domain_init()` 建立 root domain，`sbi_domain_finalize()` 固化 region，随后
`sbi_hart_protection_configure()` 写入 PMP。这里有两个常见误解：

- PMP 不是 MMU；它按物理地址限制访问，不能替代 S-mode 的 `satp` 页表；
- OpenSBI 不是天然“隐藏所有设备”；FDT、domain region 和平台配置共同决定 S-mode
  能否访问某段地址。

## 7. 初始化生命周期

当前 `lib/sbi/sbi_init.c` 的冷启动顺序可概括为：

```text
sbi_scratch_init
  -> sbi_heap_init
  -> sbi_domain_init
  -> sbi_hsm_init(cold)
  -> 唤醒其它 HART
  -> platform early init
  -> hart feature/delegation init
  -> PMU / debug trigger
  -> irqchip / IPI / TLB / timer
  -> FWFT / MPXY / domain finalize
  -> platform final init / SSE
  -> ecall init
  -> 打印信息 / SBIUnit
  -> domain startup / hart protection
  -> sbi_hsm_hart_start_finish -> next stage
```

warmboot 只执行当前 HART 所需的初始化，然后在 HSM 状态机中完成启动。这里的顺序
很重要：ecall 注册要晚于平台设备注册，这样 SRST/DBCN 等 extension 才能根据真实
设备决定是否可用；PMP 配置要靠近最后，以免初始化阶段把自己锁死。

## 8. 代码阅读地图

| 问题 | 首文件 | 继续跟踪 |
|---|---|---|
| OpenSBI 如何进来 | `firmware/fw_base.S` | `fw_dynamic.S`, `fw_dynamic.h` |
| 当前 HART 状态在哪里 | `include/sbi/sbi_scratch.h` | `sbi_scratch.c`, trap 汇编 |
| 谁决定下一级模式 | `lib/sbi/sbi_hart.c` | `sbi_init.c`, `fw_next_mode()` |
| ecall 如何找到 handler | `lib/sbi/sbi_ecall.c` | `lib/sbi/objects.mk`, `sbi_ecall_*.c` |
| 多核如何同步 | `sbi_ipi.c`, `sbi_tlb.c` | 平台 IPI 设备 |
| S-mode 能访问什么 | `sbi_domain.c` | `sbi_hart_pmp.c`, FDT domain fixup |
| FU740 如何 reset | `platform/generic/sifive/fu740.c` | FDT I2C、`sbi_system.c` |

## 9. 三个必须保持的不变量

1. `mscratch`、stack 和 hart-to-scratch 映射必须在每个 HART 上一致，否则 trap 会把
   现场保存到别的 HART；
2. `fw_dynamic_info` 的字段 offset、XLEN 和 `next_mode` 必须与前级完全匹配，否则
   OpenSBI 可能跳到错误地址或错误特权级；
3. 任何跨 HART 的请求都必须经过 IPI/原子/内存序，而不能只修改远端 HART 的普通全局
   变量。

下一步阅读：[firmware-boot.md](firmware-boot.md)、[ecall-extensions.md](ecall-extensions.md)。
