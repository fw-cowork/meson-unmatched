# OpenSBI 新平台移植检查清单

本文面向“把 OpenSBI 搬到另一块 RISC-V 板卡/SoC”的实际工作。目标不是复制 FU740
文件，而是判断哪些能力可以复用 generic，哪些必须新增平台代码。官方入口是
[Platform Guide](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_guide.md)
和 [Platform Requirements](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_requirements.md)。

## 1. 先确认平台是否满足基础要求

在写任何 OpenSBI C 代码之前，先完成硬件/手册核对：

| 要求 | 必须确认的事实 | 失败后果 |
|---|---|---|
| ISA | 每个 HART 至少 RV32/64 IM(A) + Zicsr，或等价的 ZALRSC | 原子/CSR 代码无法启动 |
| S-mode | 至少一个 HART 支持 S-mode | 无法作为 SBI SEE 启动下一级 |
| MTVEC | 所有 HART 支持 direct mode | trap 入口不能可靠建立 |
| PMP | 可选，但缺失时不能隔离 M-mode firmware | domain/PMP 安全边界下降 |
| TIME | `time` CSR 或 64-bit MMIO counter | Timer/SBI TIME 无法实现 |
| 多核 IPI | 能注入 M-mode software interrupt | HSM/RFENCE/多核启动无法完成 |
| 复位 | 至少一个可访问 reset/watchdog/PMIC 路径 | SRST 不应注册或返回不支持 |
| FDT | 能描述 CPU、memory、timer、IPI、irqchip、serial | generic 平台无法发现设备 |

“芯片有某个寄存器”不等于“OpenSBI 能使用该功能”：还要确认 M-mode 可访问、地址
不会被下一级覆盖、初始化时钟和复位已经可用。

## 2. 选择平台实现策略

```text
新板与已有 generic 设备完全兼容？
  ├─ 是 -> 只准备正确 FDT，使用 PLATFORM=generic
  └─ 否
      ├─ 只是 compatible/quirk/reset/TLB 差异？
      │    └─ generic + platform override
      ├─ 设备类型相同但寄存器实现不同？
      │    └─ 新增 lib/utils 设备 driver + FDT match
      └─ 生命周期、PMP、HART 启动完全不同？
           └─ 新增独立 platform/<vendor>/<board>
```

当前 FU740 属于第二类：generic 负责 FDT/HART/设备发现，`sifive/fu740.c` 只改 final
init 和 TLB flush limit，并注册 DA9063 reset driver。

## 3. 新增独立平台的文件契约

官方 platform guide 要求至少有：

```text
platform/<vendor>/<board>/
  Kconfig
  configs/defconfig
  objects.mk
  platform.c
```

### 3.1 `Kconfig`

定义平台开关和依赖：

```text
config PLATFORM_VENDOR_BOARD
    bool "Vendor Board support"
    select FDT
    select FDT_TIMER       # 仅在确实使用时选择
    select FDT_IRQCHIP
```

不要为了让编译通过而无条件 select 所有 FDT driver。依赖应表达真实硬件能力，避免
最终 firmware 带入无法初始化的设备和错误的 extension。

### 3.2 `configs/defconfig`

放默认构建选择：平台名、必要 driver、SBI extension、firmware 类型。新平台第一版
建议只打开：

```text
至少选择与前级交接匹配的 FW_DYNAMIC 或 FW_JUMP（也可以并行构建多种 firmware）
Base extension（OpenSBI 通常无条件构建）
SBI_ECALL_TIME（有 timer 时）
SBI_ECALL_IPI/HSM/RFENCE（多核时）
SBI_ECALL_SRST（有可验证 reset device 时）
```

PMU、SUSP、SSE、MPXY、DBTR 等先确认设备和用例，再逐项开启。

### 3.3 `objects.mk`

它同时负责对象、编译选项和 firmware 选择：

```make
platform-objs-y += platform.o
platform-objs-$(CONFIG_PLATFORM_VENDOR_BOARD) += vendor_board.o
FW_DYNAMIC=y
```

`FW_DYNAMIC=y`、`FW_JUMP=y` 和 `FW_PAYLOAD=y` 是 firmware 构建变量，不一定是
Kconfig symbol；实际选择要和前级加载方式、下一级地址以及 DTB 传递方式一起决定。

核对生成的 carray 和最终 ELF；只加入 `platform-objs` 但未在 Kconfig 打开时，文件
不会进入固件。

### 3.4 `platform.c`

实现 `struct sbi_platform` 和 `struct sbi_platform_operations`。优先使用现有公共
函数：

```text
cold_boot_allowed
nascent_init / early_init / final_init
extensions_init / domains_init
irqchip_init / timer_init / pmu_init
get_tlbr_flush_limit / get_tlb_num_entries
```

平台 hook 应该描述“硬件差异”，不要在这里复制 `sbi_init()`、ecall 分发或 HSM 状态机。

## 4. FDT 移植顺序

### 4.1 最小可启动 FDT

第一版只保留：

```text
/model
/cpus     每个 enabled CPU 的 hart ID
/memory   OpenSBI 和下一级可用 RAM
/chosen   bootargs/opensbi-config（如使用）
timer     timebase-frequency + compatible/reg
ipi       MSWI/CLINT/ACLINT 或平台 IPI
serial    early console
irqchip   PLIC/APLIC/IMSIC（若需要）
```

先让 OpenSBI 打印 banner 和 HART count，再逐项加入 PMU、I2C、reset、GPIO、domain。

### 4.2 HART ID 检查

generic 平台使用 `hart_index2id[]` 映射。检查：

```text
FDT /cpus/*/reg 是硬件 hart ID，而不是数组 index
status = "disabled" 的 CPU 不进入 platform.hart_count
boot_hart 在映射表中存在
SBI HSM caller 使用 hart ID，不要误用 hart index
```

建议在 `fw_platform_init()` 断点打印 `platform.hart_count` 和每个
`generic_hart_index2id[i]`，再在 `sbi_hartid_to_hartindex()` 验证反向查找。

## 5. 设备移植顺序

### 5.1 串口

串口是第一优先级，因为没有 early console 时后续错误难以区分。确认：

1. FDT `compatible/reg/clock` 正确；
2. `fdt_serial_init()` 能匹配驱动；
3. M-mode 访问权限和时钟已打开；
4. OpenSBI banner 能稳定输出；
5. 下一级仍可接管同一 UART，不会互相改变波特率。

### 5.2 Timer

确认 `timebase-frequency`、counter 位宽、比较器和中断路由。跟踪：

```text
generic_early_init
  -> sbi_timer_init
     -> fdt_timer_init
        -> timer device
TIME ecall
  -> sbi_timer_event_start
```

如果硬件没有 `time` CSR，必须提供 64-bit MMIO counter；如果实现 Sstc，则验证
`stimecmp` 是否可写且 timer interrupt delegation 正确。

### 5.3 IPI/irqchip

先验证单个软件中断能从 M-mode raw device 到达目标 HART，再验证 HSM 和 RFENCE。不要
直接从 RFENCE 开始，因为它同时依赖 IPI、scratch、FIFO、锁和 HART mask。

### 5.4 Reset/suspend

reset device 通过 `sbi_system_reset_add_device()` 注册，extension 根据
`system_reset_supported()` 动态可见。设备不存在时，返回 `SBI_ERR_NOT_SUPPORTED`
比伪造一个“成功但不复位”的 handler 更正确。

## 6. 内存、链接和保护

移植时必须绘制四张地址图：

```text
firmware link address / load address
firmware text-rodata / rw-bss 分界
per-HART scratch + stack + shared heap
next stage entry + FDT + payload
```

检查项：

- `FW_TEXT_START` 是否与前级加载地址一致；
- PIE relocation 是否会覆盖仍在运行的前级 HART；
- `_fw_start/_fw_end` 是否覆盖全部固件而不吞掉 FDT；
- scratch/stack 是否按 CBOM/cacheline 对齐；
- root domain 中 firmware region 是 M-only，next stage region 具有 S/U execute；
- `next_addr` 在 `sbi_domain_finalize()` 中可执行；
- PMP 配置最后执行，且不会阻止 `mret` 前的 FDT/device 访问。

## 7. 多 HART 移植验收

按以下顺序扩大范围：

```text
单 HART OpenSBI banner
单 HART -> S-mode U-Boot/Linux
多 HART 进入 OpenSBI，只有一个 coldboot
HSM GET_STATUS
HSM HART_START/STOP
IPI 发送/清除
RFENCE fence.i / sfence.vma
SBI timer tick
```

每一步都记录 hart ID、hart index、scratch 地址和返回 error。多 HART 偶发错误优先
检查映射、原子操作、cacheline 对齐和 IPI raw clear，而不是扩大栈或加入延时。

## 8. 构建和测试矩阵

| 级别 | 命令/方法 | 通过标准 |
|---|---|---|
| 静态 | `readelf -h/-l/-S`, `nm`, `objdump` | entry/section/relocation 合理 |
| 编译 | `make PLATFORM=<board> O=...` | 无 warning-as-error/未解析符号 |
| SBIUnit | `CONFIG_SBIUNIT=y ... run` | 纯函数和 mock 测试通过 |
| QEMU | 对应 machine + GDB | entry/trap/ecall 可断点 |
| baremetal | S-mode caller | Base/Time/console ABI 正确 |
| 板级串口 | OpenSBI banner | FDT/HART/timer/设备发现正确 |
| 板级 reset | reboot/shutdown | PMIC/watchdog 真正动作 |
| Linux | earlycon、SMP、timer、reboot | 启动、调度、关机无回归 |

## 9. 提交前检查

```text
[ ] 记录 OpenSBI revision、工具链和构建变量
[ ] Kconfig 依赖没有为了编译通过而过度开启
[ ] objects.mk、carray、最终 ELF 三者一致
[ ] FDT compatible 和实际设备手册一致
[ ] 单 HART 和多 HART 都测过
[ ] Base PROBE_EXT 和错误码行为已记录
[ ] domain/PMP region 有地址图
[ ] reset 实验有串口/JTAG 恢复方案
[ ] 新增 SBI handler 有成功和错误路径测试
[ ] 没有把平台寄存器判断散落进通用 ecall
```

完整移植后再阅读 [library_usage.md](https://github.com/riscv-software-src/opensbi/blob/master/docs/library_usage.md)，
理解如何把 `libsbi.a`/`libplatsbi.a` 集成到非 reference firmware 中。

相关本地文档：[platform-fu740.md](platform-fu740.md)、[source-annotations.md](source-annotations.md)、
[labs.md](labs.md)。
