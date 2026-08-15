# OpenSBI 源码导读与实验

本文把 OpenSBI 的“从入口到 SBI 服务”的代码路径串起来。建议打开同一版本的
`src/opensbi` 源码阅读；本仓库当前版本由 [OpenSBI revision](../../scripts/litebuild.py)
固定。

这是快速源码地图，不再承担全部背景解释。需要系统理解时按专题阅读：
[架构](architecture.md) -> [firmware 启动](firmware-boot.md) ->
[ecall 扩展](ecall-extensions.md) -> [ABI 速查](sbi-abi-reference.md) ->
[FU740 平台](platform-fu740.md) -> [源码级索引](source-annotations.md) ->
[调试与测试](debug-testing.md) -> [动手实验](labs.md) -> [移植清单](porting-checklist.md)。
安全边界相关的 domain/PMP、trap delegation 和共享内存访问，单独看
[security-model.md](security-model.md)。

## 1. 先画出源码树

```text
firmware/
  fw_base.S             # 所有 firmware 共用的 M-mode 入口和 trap 汇编
  fw_dynamic.S          # 从 a2/fw_dynamic_info 读取下一级信息
  fw_jump.S             # 固定地址下一级
  fw_payload.S          # 内嵌 payload 的下一级

include/sbi/
  fw_dynamic.h          # FW_DYNAMIC ABI 数据结构
  sbi_platform.h        # platform hook 契约
  sbi_trap.h             # trap context 和 handler
  sbi_ecall*.h           # extension ID、handler、返回值
  sbi_hart*.h            # HART、CSR、PMP、模式切换
  sbi_{timer,ipi,hsm,domain}.h

lib/sbi/
  sbi_init.c             # coldboot/warmboot 和最终跳转
  sbi_trap.c             # trap 分类和 S-mode ecall 入口
  sbi_ecall.c            # extension 注册和分发
  sbi_ecall_*.c          # Base/Time/IPI/RFENCE/HSM/SRST 等实现
  sbi_hart.c             # mstatus/mepc/mtvec/PMP 和 hart switch
  sbi_{timer,ipi,hsm,domain}.c
  tests/                 # SBIUnit 单元测试

lib/utils/
  fdt/、serial/、timer/、irqchip/、i2c/ # FDT 设备驱动和公共工具

platform/generic/
  platform.c             # generic FDT 平台入口
  sifive/fu740.c         # FU740 reset/PMIC/TLB errata hook
  objects.mk             # 平台对象、编译选项和 firmware 选择
```

## 2. 启动入口：从 `_start` 到 `sbi_init`

### 2.1 前级传入的寄存器

OpenSBI firmware 入口遵循 RISC-V 启动约定：

| 寄存器 | FW_DYNAMIC 含义 |
|---|---|
| `a0` | boot HART ID |
| `a1` | 前级传入的 FDT 地址 |
| `a2` | `struct fw_dynamic_info *` |
| `a3/a4` | 由具体 firmware/前级约定，通用路径不依赖 |

`firmware/fw_dynamic.S:fw_boot_hart()` 先检查 `magic` 和 `version`，再读取
`boot_hart`。之后 `fw_save_info()` 保存 `next_addr`、`next_mode`、`options` 和
`boot_hart`，供 `fw_base.S` 的通用流程读取。

当前 `struct fw_dynamic_info` 的字段顺序是：

```c
magic, version, next_addr, next_mode, options, boot_hart
```

`include/sbi/fw_dynamic.h` 对每个字段使用 `assert_member_offset()`，因此前级
SPL 和 OpenSBI 在 RV32/RV64 下必须使用完全一致的布局。当前 magic 是 `0x4942534f`
（`OSBI`），当前最大 version 是 2。

### 2.2 通用入口的关键动作

`firmware/fw_base.S` 的阅读顺序：

1. `_reset_regs`：清理初始寄存器和临时状态；
2. 设置 `_start_hang` 作为早期临时 `mtvec`；
3. 建立临时栈并清零 BSS；
4. 调用 `fw_save_info` 保存前级参数；
5. 调用 `fw_platform_init(a0..a4)`，允许平台基于 FDT 修改 platform instance；
6. 根据 HART 数量、stack size、heap size 初始化每个 HART 的 `sbi_scratch`；
7. 计算并保存 `next_arg1`、`next_addr`、`next_mode` 和 firmware options；
8. 必要时把 FDT 从旧地址复制到下一级约定地址；
9. 进入 `_start_warm`，最后调用 C 层 `sbi_init()`。

这里最容易混淆的是三类地址：OpenSBI 自身的 `_fw_start`、每个 HART 的 scratch/stack，
以及下一级软件的 `next_addr`。修改 linker script 或 `FW_TEXT_START` 时，三者必须
一起检查。

### 2.3 coldboot 与 warmboot

`lib/sbi/sbi_init.c:sbi_init()` 不直接假设 HART 0 一定是 coldboot。它使用
`coldboot_lottery` 原子变量，在满足 `next_mode` 的 HART 中选出一个 coldboot HART，
其他 HART 进入 `init_warmboot()` 等待状态。这样能覆盖多 HART 同时进入 OpenSBI 的情况。

继续阅读：

```text
sbi_init()
  -> init_coldboot()
     -> sbi_domain_root_add_memrange / sbi_domain_init()
     -> sbi_hart_init()
     -> sbi_ecall_init()
     -> sbi_timer_init(), sbi_ipi_init(), sbi_hsm_init()
     -> sbi_hart_switch_mode(... next_addr, next_mode ...)
  -> init_warmboot()
     -> wait_for_coldboot()
     -> sbi_hsm_hart_start_finish() / warm startup
```

具体初始化顺序以当前源码为准；阅读时要注意平台 hook 可能在通用初始化前后被调用。

## 3. Trap 和 ecall 路径

### 3.1 汇编保存现场

`firmware/fw_base.S` 的 trap 汇编先交换 `tp` 和 `mscratch`，找到当前 HART 的
`sbi_scratch`，再把 GPR、`mepc`、`mstatus`、cause、tval 等保存到
`struct sbi_trap_context`。C handler 返回的 context 指针随后用于恢复现场。

不能把 `sbi_trap_handler()` 当成普通 C 函数理解：它的输入是汇编布局定义的结构体，
其前置条件包括 `mscratch`、当前 HART stack 和 M-mode 中断状态。

### 3.2 C 层分类

`lib/sbi/sbi_trap.c:sbi_trap_handler()` 根据 `mcause` 区分：

- M-mode timer/software/external interrupt：分别进入 timer、IPI、irqchip 处理；
- S-mode 或 HS-mode ecall：进入 `sbi_ecall_handler()`；
- illegal instruction、misaligned load/store 等：按配置模拟或返回错误；
- 其他异常：打印 trap 信息并按平台策略处理。

### 3.3 Extension 分发

`lib/sbi/sbi_ecall.c:sbi_ecall_handler()` 从 trap context 读取：

```c
extension_id = regs->a7;
function_id  = regs->a6;
```

然后从 `ecall_exts_list` 找到 `struct sbi_ecall_extension`，调用其 `handle()`。
handler 返回 SBI error，结果写入 `a0/a1`，`mepc` 增加 4，恢复后回到 ecall 后的
S-mode 指令。

建议第一个单步目标选 Base extension：

```text
lib/sbi/sbi_ecall_base.c
  GET_SPEC_VERSION
  GET_IMP_ID
  GET_IMP_VERSION
  PROBE_EXT
```

然后再看 `sbi_ecall_time.c`、`sbi_ecall_ipi.c`、`sbi_ecall_hsm.c` 和
`sbi_ecall_srst.c`，分别连接 timer、IPI、HART 状态和平台 reset device。

## 4. 平台适配：generic 和 FU740

### 4.1 generic FDT 平台

`platform/generic/platform.c:fw_platform_init()` 读取前级传入的 FDT：

1. 匹配 root compatible 并运行 platform override；
2. 枚举 `/cpus`，建立 HART ID 到 index 的映射；
3. 根据 `/chosen/opensbi,config` 读取 heap 和 cold-boot-harts；
4. 计算 platform heap、HART count 和 CBOM block size；
5. 返回原始 FDT 地址交给后续通用流程。

UART、timer、IPI 和 irqchip 通常不是写死在 generic platform.c 中，而是由
`lib/utils/fdt` 的匹配驱动根据 FDT 初始化。这是 OpenSBI 能够复用同一 generic
firmware 的关键。

### 4.2 FU740 hook

`platform/generic/sifive/fu740.c` 当前重点是：

- 通过 FDT 查找 DA9063 PMIC，注册 cold reboot/shutdown device；
- 提供 FU740 TLB errata 对应的 flush limit hook；
- 通过 generic platform 的 final init 接入上述设备。

它不是 DDR、PCIe、U-Boot 串口或 Linux 驱动的所有者。要验证这个边界，可以对照：

```text
U-Boot SPL       -> DDR、SPL 串口、FIT 加载
OpenSBI          -> M-mode runtime、PMIC reset、SBI ecall
U-Boot proper    -> PCIe/USB/网络枚举和启动策略
Linux            -> 正式设备驱动和中断子系统
```

## 5. 编译、ELF 和日志实验

### 5.1 基线构建

```bash
./build.sh check
./build.sh opensbi-fw
```

检查产物：

```bash
file deploy/fw_dynamic.elf deploy/fw_dynamic.bin
riscv64-freedomusdk-linux-readelf -h deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-readelf -S deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-nm -n deploy/fw_dynamic.elf | \
  rg '_start|fw_boot_hart|fw_platform_init|sbi_init|sbi_trap_handler|sbi_ecall_handler'
```

重点观察 `.entry`、`.text`、`.rodata`、`.data`、`.bss`、`_fw_start`、`_fw_end` 和
`FW_TEXT_START` 是否符合当前内存图。`fw_dynamic.bin` 是展开镜像，不能替代 ELF 做
符号级调试。

### 5.2 QEMU 单步

仓库已有 QEMU GDB 入口：

```bash
# 终端 1
./qemu-gdb.sh --build

# 终端 2
gdb-multiarch deploy/qemu/fw_dynamic.elf
(gdb) target remote 127.0.0.1:1234
(gdb) b fw_platform_init
(gdb) b sbi_init
(gdb) b sbi_trap_handler
(gdb) b sbi_ecall_handler
(gdb) continue
```

建议先观察 coldboot 进入 `sbi_init()`，再从 Linux/U-Boot 的 `ecall` 触发
`sbi_trap_handler()`。如果断点地址无法命中，先用 `info files`、`readelf -h` 和
`maintenance info sections` 检查 ELF 与实际加载地址是否匹配。

### 5.3 板上串口证据

Unmatched 构建使用 `FW_OPTIONS`/SPL scratch options 控制 OpenSBI 启动打印。启动日志
中重点保存：

```text
OpenSBI ...
Platform Name
Platform HART Count
Platform Timer Device
Platform IPI Device
Firmware Base / Size
Runtime SBI Version
Domain0 ...
Handing control to U-Boot
```

不要把 `DEBUG=1` 和 runtime boot prints 混为一谈：前者是构建调试选项，后者由
firmware options 控制。

## 6. OpenSBI 自带单元测试

OpenSBI 提供 SBIUnit。实现和示例位于：

```text
include/sbi/sbi_unit_test.h
lib/sbi/tests/sbi_unit_test.c
lib/sbi/tests/sbi_*_test.c
docs/writing_tests.md
```

学习测试框架的顺序：

1. 阅读 `sbi_unit_test.h` 的 `SBIUNIT_EXPECT_*` 和 `SBIUNIT_ASSERT_*`；
2. 阅读 `lib/sbi/tests/sbi_math_test.c`、`sbi_bitops_test.c` 等纯函数测试；
3. 阅读 `sbi_console_test.c` 的设备结构 mock；
4. 在 `lib/sbi/tests/objects.mk` 注册 suite；
5. 用 `CONFIG_SBIUNIT` 构建 generic 平台并运行 `make PLATFORM=generic run`。

这套测试主要验证 M-mode 通用库，不替代 Unmatched 的 PMIC、FDT、HART 或串口上板
测试。平台 hook 应同时做 host/QEMU 级别的 SBIUnit 和板上启动回归。

## 7. 推荐的代码实验

### 实验 1：只做符号和启动日志分析

不改源码，完成基线构建，记录：

```text
OpenSBI commit
Firmware Base/Size
Runtime SBI Version
Standard SBI Extensions
Platform HART Count
next_addr/next_mode 对应的 U-Boot 入口和模式
```

### 实验 2：跟踪一次系统复位

从 U-Boot `reset` 或 Linux `sbi_system_reset` 入口开始，依次跟踪：

```text
sbi_ecall_srst.c
  -> sbi_system_reset()
  -> registered reset device
  -> platform/generic/sifive/fu740.c
  -> DA9063 I2C register access
```

在真实板上做该实验前，先确认 PMIC 和回退方式；不要在没有串口/JTAG 的情况下盲目
修改 reset hook。

### 实验 3：增加一个安全的 boot log

在平台 final init 中增加一次只读诊断输出，例如 FDT model 或 HART count，然后：

1. 只构建 `./build.sh opensbi-fw`；
2. 检查 `git diff` 和 ELF 符号；
3. 在 QEMU 或板上观察日志；
4. 再构建依赖 OpenSBI 的 U-Boot FIT；
5. 确认 Linux `earlycon=sbi`、reset 和多 HART 行为不回归。

### 实验 4：新增 SBIUnit

选择无硬件副作用的 `lib/sbi` 小函数，按照
[`docs/writing_tests.md`](https://github.com/riscv-software-src/opensbi/blob/master/docs/writing_tests.md)
添加测试。先让测试通过，再故意改变期望值确认失败路径确实能被报告。

## 8. 修改边界和提交纪律

- 改 SBI extension 行为：同时对照 SBI spec、`include/sbi/sbi_ecall_interface.h` 和
  对应 `sbi_ecall_*.c`；
- 改 HART/timer/IPI：必须考虑 coldboot、warmboot、并发和每-HART scratch；
- 改平台：优先加 FDT match/driver 或 platform hook，不要把设备寄存器散落进通用库；
- 改 firmware 汇编：先检查 RV32/RV64、PIC/PIE、linker symbol 和 trap frame offset；
- 改 domain/PMP：先画出 S-mode 可访问的 RAM/MMIO 区域，再做安全回归；
- 改版本：更新 `OPEN_SBI_REV`，保留构建日志和 ABI/API 变化记录。

最终应把每次实验拆成一个可回退的提交：源码改动、配置改动、测试结果和启动日志
分开记录。这样能区分“OpenSBI 通用逻辑变化”和“FU740 平台适配变化”。
