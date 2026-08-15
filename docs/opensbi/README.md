# OpenSBI 学习资料区

这里是本仓库的 OpenSBI 学习入口，目标是从 RISC-V 特权级和 SBI 调用约定开始，
一路读到 OpenSBI 的汇编入口、通用运行时、平台适配和 Unmatched 启动链。资料优先
使用 OpenSBI 和 RISC-V 官方仓库；本地源码以构建脚本固定的版本为准。

## 1. 本项目使用的版本

当前构建脚本固定 OpenSBI commit：

```text
74434f255873d74e56cc50aa762d1caf24c099f8
```

对应配置在 [`scripts/litebuild.py`](../../scripts/litebuild.py) 的 `OPEN_SBI_REV`。
构建目录关系如下：

```text
src/opensbi/                         # OpenSBI 工作树，构建时获取
out/opensbi/                         # O=... 的增量构建目录
out/opensbi/platform/generic/firmware/
  ├── fw_dynamic.elf                 # ELF，适合 objdump/GDB
  ├── fw_dynamic.bin                 # 展开后的固件镜像
  ├── fw_jump.elf / fw_jump.bin
  └── fw_payload.elf / fw_payload.bin
deploy/fw_dynamic.elf                # 构建脚本复制的部署产物
deploy/fw_dynamic.bin
```

## 0. 从哪里开始

如果你是第一次学习 OpenSBI，直接按 [OpenSBI 从零开始](getting-started.md) 操作。
最短路径只有四步：

```bash
./build.sh check
./build.sh opensbi-fw
./qemu-gdb.sh --build
gdb-multiarch deploy/qemu/fw_dynamic.elf
```

然后按这个顺序阅读：

```text
getting-started.md       # 第一周每天读什么、做什么、验收什么
architecture.md          # M/S/U、SBI、trap、scratch、domain
firmware-boot.md         # SPL -> FW_DYNAMIC -> mret
ecall-extensions.md      # ecall -> extension handler
sbi-abi-reference.md     # 参数和错误码查表
labs.md                  # 从观察到修改的实验
```

第一天只做构建、ELF 检查和启动链绘图，不要修改 `fw_base.S`、PMP 或 PMIC reset。
先能解释 `a0/a1/a2`、`next_addr/next_mode` 和 `mepc`，再开始单步和改代码。

首次学习可以执行：

```bash
./build.sh fetch-sources
./build.sh opensbi-fw
```

`src/opensbi` 和 `out/opensbi` 属于生成目录，不要把里面的临时修改当作仓库补丁。
要做可复现修改，应把变更整理成 OpenSBI patch，再接入构建脚本的开发流程。

## 2. 先建立正确的心智模型

SBI 是运行在 M-mode 的平台固件与 S-mode/HS-mode 软件之间的接口。OpenSBI 是这个
接口的开源参考实现，不是 Linux 内核、U-Boot 的库函数，也不是负责所有硬件初始化
的 BIOS。

```text
M-mode:  OpenSBI
         ├── 处理 S-mode ecall
         ├── 管理定时器、IPI、RFENCE、HSM、系统复位等特权资源
         ├── 通过平台 hook 连接 FU740/PMIC/FDT 设备
         └── 设置 M-mode -> S-mode 的启动现场

S-mode:  U-Boot proper / Linux
         ├── 通过 a7/a6/a0-a5 发起 SBI ecall
         ├── 在 a0/a1 读取 error/value
         └── 负责自己的 MMU、驱动、调度和用户空间
```

OpenSBI 官方实现由几个边界清晰的部分组成：

| 层次 | 本地源码 | 学习重点 |
|---|---|---|
| Firmware 入口 | `firmware/` | `_start`、重定位、FDT、scratch、跳转下一级 |
| SBI 通用实现 | `lib/sbi/` | trap、ecall、扩展注册、HSM、timer、IPI、domain |
| 公共接口 | `include/sbi/` | 数据结构、CSR、SBI error/extension ID、平台契约 |
| 平台适配 | `platform/` | `struct sbi_platform`、平台初始化、quirk、reset |
| FDT/设备工具 | `lib/utils/` | UART、timer、IPI、PLIC、I2C、PMIC 等设备驱动 |
| 固件类型 | `docs/firmware/`、`firmware/fw_*.S` | FW_DYNAMIC、FW_JUMP、FW_PAYLOAD 的差异 |

## 3. 推荐学习路线

本资料区按“概念 -> 启动 -> 服务 -> 平台 -> 验证”拆成专题，先看本页建立地图，再按
下表深入。`source-guide.md` 保留为快速源码导航；细节以专题文档为准。

| 顺序 | 专题 | 你要回答的问题 |
|---|---|---|
| 1 | [architecture.md](architecture.md) | 为什么 S-mode 需要 SBI？M/S、trap、scratch、domain 如何协作？ |
| 2 | [firmware-boot.md](firmware-boot.md) | SPL 如何交接 FW_DYNAMIC？OpenSBI 如何 relocation、coldboot 并 `mret` 回 U-Boot？ |
| 3 | [ecall-extensions.md](ecall-extensions.md) | 一条 ecall 如何分发到 TIME/IPI/RFENCE/HSM/SRST handler？ |
| 4 | [sbi-abi-reference.md](sbi-abi-reference.md) | 每个常用 extension 的寄存器参数、返回值和错误码是什么？ |
| 5 | [platform-fu740.md](platform-fu740.md) | generic FDT 和 FU740 override 做了什么？PMIC/TLB hook 的边界在哪里？ |
| 6 | [debug-testing.md](debug-testing.md) | 如何用 ELF、QEMU/GDB、SBIUnit 和 Unmatched 回归验证？ |
| 7 | [labs.md](labs.md) | 如何从只读分析逐步做到 ecall、SBIUnit、PMIC 和 vendor extension 实验？ |
| 8 | [source-annotations.md](source-annotations.md) | 按执行顺序定位函数、共享状态、初始化边界和故障切分点？ |
| 9 | [porting-checklist.md](porting-checklist.md) | 换一台 RISC-V 机器时，哪些能力复用 generic，哪些需要新平台？ |
| 10 | [security-model.md](security-model.md) | 为什么要做 domain/PMP？S-mode 地址、trap delegation 和共享内存如何受保护？ |

### 阶段 A：RISC-V 前置知识

先掌握 M/S/U 特权级、`mstatus`/`mepc`/`mcause`/`mtvec`、`medeleg`/`mideleg`、
`mscratch`、`satp`、PMP，以及 RISC-V 调用约定和 ELF/PIE。OpenSBI 的汇编入口和
trap 保存现场都建立在这些规则上。

推荐资料：

- [RISC-V Instruction Set Manual](https://github.com/riscv/riscv-isa-manual)：Volume II
  Privileged Architecture 是 CSR、trap、特权级和 PMP 的依据；
- [RISC-V ELF psABI](https://github.com/riscv-non-isa/riscv-elf-psabi-doc)：理解 `a0-a7`、
  `sp`、`gp`、`tp` 和 callee-saved 寄存器；
- [本仓库的 baremetal 设计](../architecture/baremetal-design.md)：联系当前 S-mode
  裸机程序不能直接调用 M-mode 专用资源的边界。

### 阶段 B：先读 SBI 契约，再读实现

1. 阅读 [RISC-V SBI Specification](https://github.com/riscv-non-isa/riscv-sbi-doc)。
2. 先看 Base、Timer、IPI、RFENCE、HSM、System Reset 扩展，再看 PMU、DBCN、SUSPEND
   等扩展。
3. 对照 [`include/sbi/sbi_ecall_interface.h`](https://github.com/riscv-software-src/opensbi/blob/master/include/sbi/sbi_ecall_interface.h)
   和 `lib/sbi/sbi_ecall_*.c`，建立“规范 extension ID -> OpenSBI handler”的映射。

S-mode ecall 的基本寄存器约定是：`a7` 放 extension ID，`a6` 放 function ID，`a0-a5`
放参数；返回时 `a0` 是 SBI error，`a1` 是返回值。不要把它和 C 函数 ABI 中的普通
返回值混为一谈。

### 阶段 C：读启动和固件类型

按以下顺序阅读：

1. `firmware/fw_base.S`：通用入口、临时 trap、栈、BSS、scratch、FDT 搬移；
2. `firmware/fw_dynamic.S`：读取 `a2` 指向的动态启动信息；
3. `include/sbi/fw_dynamic.h`：动态信息字段和偏移断言；
4. `docs/firmware/fw.md`、`fw_dynamic.md`：FW_DYNAMIC/FW_JUMP/FW_PAYLOAD 的边界；
5. `lib/sbi/sbi_init.c`：coldboot lottery、每个 HART 初始化和最终跳转。

当前 Unmatched 使用 FW_DYNAMIC。前级 SPL 把结构体地址放在 `a2`，OpenSBI 从中取得
`next_addr`、`next_mode`、`options` 和 `boot_hart`，最后以 S-mode 进入 U-Boot proper。
当前协议魔数是 `0x4942534f`（ASCII `OSBI`），最大动态信息版本为 2；不要沿用旧文档中
缺少末尾 `f` 的 `0x4942534` 写法。

### 阶段 D：读 trap 和 SBI 调用分发

```text
S-mode executes ecall
  -> firmware/fw_base.S 保存 GPR/CSR 到 trap context
  -> lib/sbi/sbi_trap.c:sbi_trap_handler()
  -> lib/sbi/sbi_ecall.c:sbi_ecall_handler()
  -> 根据 a7 查找注册的 struct sbi_ecall_extension
  -> extension handler 处理 a6/a0-a5
  -> a0=error, a1=value, mepc += 4
  -> 恢复现场并返回 S-mode
```

这是最值得用 GDB 单步的一条路径，因为它同时连接汇编、CSR、C 结构体和 SBI 规范。

### 阶段 E：读平台和设备模型

本项目编译 `PLATFORM=generic`，FDT 决定 UART、timer、IPI、PLIC 等设备。重点文件：

```text
platform/generic/platform.c       # FDT 解析、hart 列表、heap、平台入口
platform/generic/sifive/fu740.c   # FU740 reset/PMIC 和 TLB errata hook
platform/generic/sifive/objects.mk # FU740 平台对象加入 generic 构建
lib/utils/fdt/                    # FDT 驱动匹配和设备初始化
```

OpenSBI 的平台 hook 不是另写一套 SBI 栈，而是实现 `struct sbi_platform` 约定的
平台操作。先读 [Platform Support Guideline](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_guide.md)，
再读 [Platform Requirements](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_requirements.md)。

### 阶段 F：读 HART、timer、IPI 和 domain

建议顺序：

```text
lib/sbi/sbi_hsm.c       # hart start/stop/suspend/resume
lib/sbi/sbi_timer.c     # M-mode timer device 和 timer event
lib/sbi/sbi_ipi.c       # IPI event、software interrupt、RFENCE 协作
lib/sbi/sbi_rfence.c    # remote fence 请求
lib/sbi/sbi_domain.c    # ROOT domain、PMP region、S/U 访问边界
lib/sbi/sbi_hart.c      # CSR/PMP、切换到 next_mode
```

Domain 不是普通的“启动参数”：它会影响 HART 归属、内存/MMIO region、IPI/RFENCE/HSM
可见范围和 S/U 访问权限。初学阶段先理解 ROOT domain，再做隔离实验。

## 4. 官方资料索引

| 资料 | 用途 |
|---|---|
| [OpenSBI source](https://github.com/riscv-software-src/opensbi) | 源码、Makefile、README、所有平台 |
| [OpenSBI Library Usage](https://github.com/riscv-software-src/opensbi/blob/master/docs/library_usage.md) | `libsbi.a`、`libplatsbi.a` 和外部 firmware 的调用约束 |
| [Firmware documentation](https://github.com/riscv-software-src/opensbi/tree/master/docs/firmware) | 三类 firmware、payload 和 `FW_OPTIONS` |
| [Platform documentation](https://github.com/riscv-software-src/opensbi/tree/master/docs/platform) | generic、QEMU、SiFive 等平台说明 |
| [Platform Support Guideline](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_guide.md) | 新平台目录、Kconfig、objects.mk、platform.c |
| [Domain Support](https://github.com/riscv-software-src/opensbi/blob/master/docs/domain_support.md) | ROOT/domain、PMP region 和 FDT 配置 |
| [SBI specification](https://github.com/riscv-non-isa/riscv-sbi-doc) | extension、调用编码、error 和版本 |
| [RISC-V privileged ISA](https://github.com/riscv/riscv-isa-manual) | CSR、trap、特权级、PMP 和内存模型 |

按问题查资料时使用下面的精确入口：

| 想学习 | 官方入口 | 本地源码入口 |
|---|---|---|
| firmware 三种形态 | [Firmware](https://github.com/riscv-software-src/opensbi/blob/master/docs/firmware/fw.md) | `firmware/fw_dynamic.S`, `fw_jump.S`, `fw_payload.S` |
| FW_DYNAMIC 结构体 | [FW_DYNAMIC](https://github.com/riscv-software-src/opensbi/blob/master/docs/firmware/fw_dynamic.md) | `include/sbi/fw_dynamic.h` |
| 作为库集成 OpenSBI | [Library usage](https://github.com/riscv-software-src/opensbi/blob/master/docs/library_usage.md) | `lib/sbi/`, `lib/utils/` |
| 新增/移植平台 | [Platform guide](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_guide.md) | `platform/generic/platform.c`, `sifive/fu740.c` |
| 平台必须满足什么约定 | [Platform requirements](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_requirements.md) | `firmware/fw_base.S`, `include/sbi/sbi_platform.h` |
| domain/PMP 隔离 | [Domain support](https://github.com/riscv-software-src/opensbi/blob/master/docs/domain_support.md) | `lib/sbi/sbi_domain.c`, `sbi_hart_protection.c` |
| 写 M-mode 测试 | [Writing tests](https://github.com/riscv-software-src/opensbi/blob/master/docs/writing_tests.md) | `lib/sbi/tests/`, `include/sbi/sbi_unit_test.h` |

本地资料之间的依赖关系是：`source-annotations.md` 负责“代码从哪里走”，
`sbi-abi-reference.md` 负责“寄存器怎么传”，`labs.md` 负责“如何验证”，
`porting-checklist.md` 负责“如何迁移到新平台”。
`security-model.md` 负责“哪些输入不能直接信任、哪些访问必须临时授权”。

官方 OpenSBI README 说明了 `libsbi.a` 的平台无关实现、`libplatsbi.a` 的平台集成和
reference firmware 的关系；不要只阅读某一个平台文件就把 OpenSBI 理解成“板级启动代码”。

## 5. 本仓库的关联资料

- [OpenSBI on HiFive Unmatched](../boot/opensbi-on-unmatched.md)：当前板子的启动位置、
  FW_DYNAMIC、FU740 边界和 GDB 方法；
- [Boot chain overview](../boot/boot-chain-overview.md)：ZSBL -> SPL -> OpenSBI -> U-Boot -> Linux；
- [SPL OpenSBI analysis](../boot/spl-analysis.md)：U-Boot `spl_invoke_opensbi()` 如何构造动态信息；
- [Build guide](../build/build.md)：OpenSBI 固定版本、构建产物和 QEMU/Unmatched 命令；
- [Memory map](../reference/memory-map.md)：OpenSBI、U-Boot、FDT 和 DDR 地址关系。

## 6. 学习验收标准

完成第一轮学习后，应该能够不查代码回答：

1. 为什么 U-Boot SPL 和 OpenSBI 都运行在 M-mode，而 U-Boot proper 在 S-mode；
2. `a0`、`a1`、`a2` 在 FW_DYNAMIC 入口分别携带什么；
3. `fw_dynamic_info` 的 `next_addr` 和 `next_mode` 如何变成最终的 `mepc/mstatus.MPP`；
4. S-mode `ecall` 如何从 trap 入口走到具体 SBI extension handler；
5. timer、IPI、RFENCE 为什么必须由 M-mode 运行时协调；
6. FU740 的 DDR/PCIe 为什么不属于当前 OpenSBI 平台 hook；
7. 修改 `platform/generic/sifive/fu740.c`、`lib/sbi/` 或 firmware 汇编时，分别应该如何
   构建、观察日志和验证回归。

下一步按 [`architecture.md`](architecture.md) 和 [`firmware-boot.md`](firmware-boot.md)
做源码级阅读，再用 [`sbi-abi-reference.md`](sbi-abi-reference.md) 查参数，最后按
[`labs.md`](labs.md) 完成实验闭环。

## 7. 术语和自测

| 术语 | 一句话定义 |
|---|---|
| SEE | Supervisor Execution Environment，S-mode 软件看到的执行环境，OpenSBI 是一种实现 |
| SBI | M-mode firmware 提供给 S/HS-mode 的标准调用接口 |
| HART | 一个 RISC-V 硬件线程，OpenSBI 的 scratch、stack、HSM 状态按 HART 组织 |
| FDT/DTB | 描述 CPU、内存和设备的扁平设备树；前级传入，OpenSBI 解析并可能 fixup |
| domain | 一组 HART、内存/MMIO region 和权限策略，root domain 是默认域 |
| RFENCE | Remote fence，要求目标 HART 本地执行 fence 指令的 SBI 服务 |
| HSM | Hart State Management，启动/停止/挂起其它 HART 的 SBI 服务 |
| Sstc | RISC-V timer 扩展；可用时 OpenSBI 可直接写 `stimecmp` |

完成每篇专题后，不看源码回答这些问题：

1. `fw_dynamic_info` 为什么要由 OpenSBI 复制到自己的静态数据？
2. 为什么 OpenSBI 入口 lottery 和 `sbi_init()` 的 coldboot lottery 是两次不同的竞争？
3. `mscratch`、`tp` 和每 HART stack 在 trap 入口如何互相配合？
4. 为什么 `PROBE_EXT` 成功不能推出 SRST 一定能复位板子？
5. `sbi_tlb_request()` 为什么同时需要 FIFO、IPI 和同步计数器？
6. FU740 的 DA9063 reset 应该放在 platform hook、FDT driver 还是 Linux driver？为什么？
7. 修改 `next_mode` 为 M/U 时，`sbi_hart_switch_mode()` 会改变哪些 CSR 清理动作？
