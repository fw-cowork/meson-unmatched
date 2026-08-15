# OpenSBI 函数级源码索引

这份索引按“执行顺序”列出当前固定 OpenSBI commit 中应该进入的函数。阅读时不要只
看函数名，要同时记录输入、修改的共享状态和返回到哪里。路径相对于 `src/opensbi/`。

## 1. 入口与 firmware

| 顺序 | 文件/函数 | 读什么 | 下一跳 |
|---:|---|---|---|
| 1 | `firmware/fw_dynamic.S:fw_boot_hart` | 校验 magic/version，读取 preferred boot HART | `fw_base.S` `_start` |
| 2 | `firmware/fw_base.S:_start` | 入口寄存器、临时 mtvec、栈、BSS、PIE | `fw_save_info` |
| 3 | `firmware/fw_dynamic.S:fw_save_info` | 将前级参数复制到 firmware 静态变量 | `fw_platform_init` |
| 4 | `platform/generic/platform.c:fw_platform_init` | FDT root、model、HART map、heap | `_start_warm` |
| 5 | `firmware/fw_base.S:_start_warm` | scratch、stack、mscratch、正式 mtvec | `sbi_init` |
| 6 | `lib/sbi/sbi_init.c:sbi_init` | next mode 检查和 coldboot lottery | cold/warm path |

### 1.1 入口寄存器生命周期

```text
SPL entry(hartid, fdt, &info)
  a0 = hartid
  a1 = fdt
  a2 = fw_dynamic_info*
       |
       +-- fw_boot_hart: 只允许使用 a0/a1/a2
       +-- fw_save_info: 复制 next_addr/mode/options/boot_hart
       +-- fw_platform_init: 解析 a1 指向的 FDT
       +-- scratch.next_arg1 = relocated/original FDT
       +-- sbi_hart_switch_mode: a0=hartid, a1=next_arg1
```

不要在入口早期新增需要全局 C 状态的逻辑。`fw_boot_hart` 运行时还没有正式 stack、
`mscratch` 或 BSS 语义；早期代码只能使用约定允许的寄存器和局部汇编状态。

## 2. `sbi_init()` 冷启动调用图

```text
sbi_init(scratch)
  |
  +-- validate scratch->next_mode
  +-- sbi_platform_cold_boot_allowed
  +-- atomic_xchg(coldboot_lottery)
  +-- sbi_platform_nascent_init
  |
  +-- init_coldboot
  |     +-- sbi_scratch_init
  |     +-- sbi_heap_init
  |     +-- sbi_domain_init
  |     +-- sbi_hsm_init(cold=true)
  |     +-- wake_coldboot_harts
  |     +-- sbi_platform_early_init
  |     +-- sbi_hart_init
  |     +-- sbi_pmu_init / sbi_dbtr_init
  |     +-- sbi_irqchip_init / sbi_ipi_init / sbi_tlb_init / sbi_timer_init
  |     +-- sbi_fwft_init / sbi_mpxy_init
  |     +-- sbi_domain_finalize
  |     +-- sbi_platform_final_init
  |     +-- sbi_sse_init
  |     +-- sbi_ecall_init
  |     +-- run_all_tests
  |     +-- sbi_domain_startup
  |     +-- sbi_hart_protection_configure
  |     +-- sbi_hsm_hart_start_finish
  |
  +-- init_warmboot
        +-- wait_for_coldboot
        +-- sbi_hsm_init(cold=false)
        +-- platform/hart/PMU/irq/IPI/TLB/timer warm init
        +-- sbi_hart_protection_configure
        +-- sbi_hsm_hart_start_finish
```

### 2.1 三个初始化阶段不要混淆

| 阶段 | 典型函数 | 可以做什么 | 不应做什么 |
|---|---|---|---|
| nascent | `sbi_platform_nascent_init` | 只初始化极早期 per-HART CSR/设备 | 依赖 FDT 设备链表、共享 heap |
| early | `sbi_platform_early_init` | UART、FDT early drivers、cache block | 假设 domain/PMP 已最终固化 |
| final | `sbi_platform_final_init` | FDT fixup、reset device、最终设备注册 | 在自身 PMP 隔离后访问未授权地址 |

FU740 的 override 在 final 阶段注册 DA9063 reset，再由 `sbi_ecall_init()` 决定 SRST
是否可见，这就是源码顺序而不是偶然现象。

## 3. scratch 与动态 offset

关键文件：

```text
include/sbi/sbi_scratch.h   # 固定布局和宏
lib/sbi/sbi_scratch.c       # hart map、offset allocator
firmware/fw_base.S          # 分配/写入每 HART scratch
```

`sbi_scratch_init()` 建立两张表：

```text
hartindex_to_hartid_table[index] = hardware hartid
hartindex_to_scratch_table[index] = struct sbi_scratch*
```

`sbi_scratch_alloc_offset()` 是一个只增不减的 allocator，按照平台
`cbom_block_size` 对齐，并把新区域清零到每个 HART。它不是普通 heap，也不能在热路径
中反复申请/释放。适合放 timer delta、IPI data、TLB FIFO offset 等 per-HART 状态。

读任何使用 `sbi_scratch_offset_ptr()` 的代码时，先找该 offset 的分配点，再确认：

1. 分配只发生在 coldboot；
2. warmboot 只读取已存在的 offset；
3. 所有 HART 的 scratch 都获得同样的布局；
4. 共享结构体的原子/锁不会跨 cache line 产生伪共享问题。

## 4. domain 与保护顺序

### 4.1 `sbi_domain_init()`

入口位于 `lib/sbi/sbi_domain.c:868` 附近，先建立 root domain、HART 归属和基础
firmware region。generic 平台还会调用 FDT domain fixup，把内存、MMIO、reserved-memory
和 `/chosen` 信息转换为 region。

### 4.2 `sbi_domain_finalize()`

finalize 会：

1. 检查 region 是否重叠、排序、删除被覆盖的 region；
2. 检查 domain `next_mode` 只能是 S/U；
3. 检查 `next_addr` 对目标 mode 具有 execute 权限；
4. 固化 domain 列表，供后续 HSM/DBCN/RFENCE 做权限判断。

因此 `fw_dynamic_info.next_mode=M` 可以被 firmware 入口暂存，但 root domain finalize
阶段只接受 S/U 作为下一级 domain 的启动模式。分析 M-mode handoff 时要区分
`sbi_hart_switch_mode()` 的通用能力和 domain policy 的限制。

### 4.3 PMP/SMEPMP

`sbi_hart_protection_configure()` 放在启动末尾，原因是某些实现会撤销 M-mode 对 S/U
space 的访问。调试 domain/PMP 时先看 `sbi_domain_check_addr()` 的逻辑权限，再看
`sbi_hart_pmp.c` 的物理配置，不能只读 PMP CSR 数值。

## 5. trap frame 和 ecall

| 层 | 文件/函数 | 输入/输出 |
|---|---|---|
| 汇编入口 | `firmware/fw_base.S` trap macro | `mscratch` -> scratch，保存 GPR/CSR |
| 分类 | `lib/sbi/sbi_trap.c:sbi_trap_handler` | `mcause/mtval` -> interrupt/exception |
| ecall | `lib/sbi/sbi_ecall.c:sbi_ecall_handler` | `a7/a6` -> extension/function |
| handler | `lib/sbi/sbi_ecall_*.c` | 返回 error + `out.value` |
| 汇编返回 | trap restore + `mret` | `mepc += 4` 后回 S-mode |

调试时至少记录：`mepc`、`mcause`、`mtval`、`a7`、`a6`、`a0`、`a1`、当前 hartid 和
`mscratch`。只记录 `a0` 不足以判断是 SBI error 还是扩展返回值。

## 6. Timer/IPI/TLB 的依赖图

```text
TIME ecall
  -> sbi_timer_event_start
     -> stimecmp 或 timer_dev

IPI ecall
  -> sbi_ipi_send_smode
     -> sbi_ipi_send_many
        -> remote scratch.ipi_type
        -> platform IPI raw send
        -> remote sbi_ipi_process

RFENCE ecall
  -> sbi_tlb_request
     -> TLB FIFO + sync counter
     -> IPI_TLB
     -> local fence.i/sfence/hfence
```

这张图可以用来定位“设备不工作”还是“通用 runtime 不工作”：TIME 先看 timer device，
IPI 先看 platform IPI，RFENCE 则还要检查 target domain、FIFO 内存和同步计数器。

## 7. ecall 注册的链接器路径

当前 `lib/sbi/objects.mk` 的链路是：

```text
CONFIG_SBI_ECALL_TIME=y
  -> libsbi-objs += sbi_ecall_time.o
  -> carray-sbi_ecall_exts += ecall_time
  -> sbi_ecall_exts[] (generated carray)
  -> sbi_ecall_init()
  -> ecall_time.register_extensions()
  -> sbi_ecall_register_extension()
```

新增 extension 只加入 C 文件而未加入 carray 时，代码可能成功编译但永远不会注册。
加入 carray 但 Kconfig 未打开时，则不会进入最终 ELF。检查方法：

```bash
riscv64-freedomusdk-linux-nm -n deploy/fw_dynamic.elf | rg 'ecall_|sbi_ecall_exts'
riscv64-freedomusdk-linux-strings deploy/fw_dynamic.elf | rg 'time|hsm|srst|vendor'
```

## 8. 发生故障时按调用边界切分

```text
没有任何输出
  -> fw_dynamic magic/version -> relocation -> temporary stack -> UART early init

有 OpenSBI banner，无法进入 U-Boot
  -> domain next_addr/execute -> mstatus.MPP/mepc -> FDT next_arg1

能进入 U-Boot，SBI 命令缺 extension
  -> ecall Kconfig/carray -> register_extensions capability -> platform device

extension 存在但返回错误
  -> handler 参数 -> domain check -> device operation -> SBI error mapping

多核行为异常
  -> hart map -> scratch map -> IPI raw device -> HSM state -> TLB sync
```

下一步用 [labs.md](labs.md) 按这个边界逐项验证，ABI 参数查 [sbi-abi-reference.md](sbi-abi-reference.md)。
