# SBI ecall 与扩展源码导读

本文从一条真实的 `ecall` 指令开始，说明 OpenSBI 如何保存现场、注册扩展、检查
参数、执行跨 HART 操作并返回错误码。源码版本为本仓库固定的 OpenSBI commit。

## 1. ABI 只有六个关键寄存器

现代 SBI 调用使用 RISC-V ABI：

```text
a7 = extension ID
a6 = function ID
a0-a5 = 参数
ecall
a0 = SBI error code
a1 = return value（legacy v0.1 特例除外）
```

`a0` 不是 C 函数的普通返回值，而是 SBI error。成功是 `0`，常见错误包括
`SBI_ERR_FAILED`、`SBI_ERR_NOT_SUPPORTED`、`SBI_ERR_INVALID_PARAM`、
`SBI_ERR_DENIED`、`SBI_ERR_INVALID_ADDRESS` 和 `SBI_ERR_ALREADY_AVAILABLE`。
调用者必须按规范处理负值，不能只判断 `a0 == -1`。

Base extension `PROBE_EXT` 是运行时能力探测入口。即使某个 OpenSBI 构建启用了
代码，平台没有对应设备时也可能不注册该 extension，例如 SRST 没有可用 reset device。

## 2. 从汇编 trap 到 handler

### 2.1 保存现场

`firmware/fw_base.S` 的 trap 宏先交换 `tp` 与 `mscratch`，因为 `mscratch` 指向当前
HART 的 scratch，而 `tp` 可能属于被打断的 S-mode。随后保存：

```text
GPR x1-x31
mepc / mstatus / mcause / mtval
必要时的 mtval2 / mtinst / htval / htinst
```

保存布局必须和 `include/sbi/sbi_trap.h` 一致。C handler 不能随意改变结构体字段，
否则恢复阶段会把错误值写回寄存器。

### 2.2 分类异常

`lib/sbi/sbi_trap.c:sbi_trap_handler()` 的核心分支：

| `mcause` 类型 | 处理 |
|---|---|
| M timer interrupt | `sbi_timer_process()`，必要时置 S timer pending |
| M software interrupt | `sbi_ipi_process()` |
| M external interrupt | 平台 irqchip/PLIC/IMSIC handler |
| S/HS ecall | `sbi_ecall_handler()` |
| illegal instruction | `sbi_illegal_insn()`，可模拟部分 CSR/指令 |
| misaligned load/store | `sbi_trap_ldst()`，按配置模拟 |
| 其它异常 | 双 trap 检查、打印并决定 fatal 或下转 |

### 2.3 分发和返回

`sbi_ecall.c` 维护一个按 extension ID 区间匹配的链表。注册时拒绝重叠区间；初始化
时遍历链接器生成的 `sbi_ecall_exts[]` carray，调用每个 extension 的
`register_extensions()`，由它根据平台能力缩小可用范围。

简化伪代码：

```c
ext = sbi_ecall_find_extension(regs->a7);
if (!ext)
    ret = SBI_ENOTSUPP;
else
    ret = ext->handle(regs->a7, regs->a6, regs, &out);

regs->mepc += 4;
regs->a0 = ret;
if (!legacy)
    regs->a1 = out.value;
```

`mepc += 4` 假设 ecall 是标准 32-bit 指令；handler 不应自己再次递增。fatal trap
可以通过 `out.skip_regs_update` 保留现场，普通 SBI 错误都要返回 caller。

## 3. 本版本扩展总表

ID 的权威定义在 `include/sbi/sbi_ecall_interface.h`，下表列出当前源码和作用；
实际是否出现在 banner 中还取决于 Kconfig 和平台设备。

| Extension | ID | 实现文件 | 主要作用/依赖 |
|---|---:|---|---|
| BASE | `0x10` | `sbi_ecall_base.c` | 版本、实现 ID、探测 extension、CSR ID |
| TIME | `0x54494d45` | `sbi_ecall_time.c` | `SET_TIMER`；平台 timer 或 Sstc |
| IPI | `0x735049` | `sbi_ecall_ipi.c` | hart mask/base，远端 SSIP |
| RFENCE | `0x52464e43` | `sbi_ecall_rfence.c` | fence.i/sfence/hfence + IPI/TLB FIFO |
| HSM | `0x48534d` | `sbi_ecall_hsm.c` | hart start/stop/status/suspend |
| SRST | `0x53525354` | `sbi_ecall_srst.c` | cold/warm reboot、shutdown；reset device |
| SUSP | `0x53555350` | `sbi_ecall_susp.c` | suspend；平台 suspend 能力 |
| PMU | `0x504d55` | `sbi_ecall_pmu.c` | 计数器、固件事件、共享内存 |
| DBCN | `0x4442434e` | `sbi_ecall_dbcn.c` | debug console 读写，检查 S-mode buffer |
| CPPC | `0x43505043` | `sbi_ecall_cppc.c` | 平台性能控制 |
| FWFT | `0x46574654` | `sbi_ecall_fwft.c` | firmware feature 配置 |
| LEGACY | `0x0..0x8` | `sbi_ecall_legacy.c` | SBI v0.1 兼容接口 |
| VENDOR | `0x09000000..0x09ffffff` | `sbi_ecall_vendor.c` | vendor 自定义扩展路由 |
| DBTR | `0x44425452` | `sbi_ecall_dbtr.c` | debug trigger 管理 |
| SSE | `0x535345` | `sbi_ecall_sse.c` | supervisor software events |
| MPXY | `0x4d505859` | `sbi_ecall_mpxy.c` | message proxy channel |

当前 `out/opensbi/platform/generic/kconfig/.config` 默认启用了这些 `CONFIG_SBI_ECALL_*`
项，但 `CONFIG_SBIUNIT` 未启用。新增代码不能只改 C 文件，还要同步 Kconfig、
`lib/sbi/objects.mk` 和测试配置。

## 4. 五条值得单步的调用路径

### 4.1 TIME：一个值如何变成硬件比较器

```text
s-mode a7=TIME, a6=SET_TIMER, a0=next_event
  -> sbi_ecall_time.c
  -> sbi_timer_event_start(next_event)
  -> 若有 Sstc：写 stimecmp
     否则：timer_dev->timer_event_start()
  -> 清/置 MIP/MIE 对应位
```

handler 要兼容 RV32 的 `a1:a0` 64-bit 时间值；RV64 只使用 `a0`。硬件 timer 选择
在 `sbi_timer_init()`，不是在 ecall handler 里按型号判断。

### 4.2 IPI：hart mask 到远端 SSIP

`sbi_ecall_ipi.c` 从 `a0=hart_mask`、`a1=hart_mask_base` 构造目标集合，先与当前
domain 的 interruptible HART 相交，再调用 `sbi_ipi_send_many()`。后者把事件位写入
远端 scratch 的 `ipi_type`，只有从 0 变为非 0 时才触发底层 IPI，减少重复 MMIO。

远端收到 software interrupt 后：

```text
raw_clear -> 读取/清空 ipi_type -> 逐事件 process
IPI_SMODE -> csr_set(CSR_MIP, MIP_SSIP)
IPI_HALT  -> sbi_hsm_hart_stop()
IPI_TLB   -> sbi_tlb 请求队列
```

### 4.3 RFENCE：远端刷新不是一条 fence 指令

`sbi_ecall_rfence.c` 创建 `struct sbi_tlb_info`，携带类型、地址、size、ASID/VMID 和
目标 mask，交给 `sbi_tlb_request()`。每个目标 HART 有 FIFO 和同步计数器：本地先执行
对应 `sfence.vma`/`fence.i`，远端通过 IPI 消费 FIFO，发起者等待所有计数器归零。

这解释了为什么 Linux 即使知道自己的页表地址，也不能直接让另一个 HART 执行
`sfence.vma`：另一个 HART 的 TLB 是本地状态，必须由它自己执行指令。

### 4.4 HSM：启动 HART 的状态机

HSM handler 先检查目标 HART 是否属于当前 domain，再检查状态转换是否有效：

```text
STOPPED -> START_PENDING -> STARTED
STARTED -> STOP_PENDING -> STOPPED
STARTED -> SUSPENDED（平台支持时）
```

`HART_START` 的 `a0/a1/a2` 是目标入口、opaque、DTB/参数，OpenSBI 写入目标 scratch，
通过平台 IPI/唤醒机制让它进入 warmboot，最终在目标 HART 上完成 `mret`。因此入口
函数必须满足 SBI 文档的启动约定，不能直接假设 cache/MMU 已开启。

### 4.5 SRST：extension 可用不等于设备一定存在

`sbi_ecall_srst.c` 校验 reset type/reason 后调用 `sbi_system_reset()`。系统 reset
层维护一个 device 链表，选出 `system_reset_check()` 返回支持的设备。FU740 的
`fu740.c` 通过 FDT 匹配 `dlg,da9063`，找到 I2C adapter 后注册 PMIC reset device。
没有匹配节点时 SRST 可能返回 `SBI_ENODEV` 或根本不注册 extension。

## 5. legacy v0.1 与现代 SBI

v0.1 把 timer、console、IPI、remote fence、shutdown 各自占用低 ID（0..8），没有
统一的 `a1=value` 返回约定。现代 SBI 使用可读的 32-bit ASCII extension ID 和统一
错误码/函数 ID，Base extension 负责探测能力。

OpenSBI 可以同时注册 LEGACY 和现代扩展。兼容代码在
`sbi_ecall_handler()` 中识别 legacy ID，避免向 legacy caller 写入 `a1`。新代码应
优先使用 SBI v0.3+ 扩展，不要因为 legacy 接口“能工作”就复制旧调用方式。

## 6. 添加一个 vendor extension 的最小方法

1. 选择 `SBI_EXT_VENDOR_START..END` 中属于自己组织的 ID，写入公共头文件；
2. 定义 `struct sbi_ecall_extension`，实现 `register_extensions()` 和 `handle()`；
3. 在 `lib/sbi/objects.mk` 加入 `carray-sbi_ecall_exts-*` 和对象文件；
4. handler 严格检查函数号、地址范围、hart/domain 权限和保留参数；
5. 用 `sbi_ecall_register_extension()` 注册不重叠的区间；
6. 在 SBIUnit 或 S-mode baremetal caller 中覆盖成功、未支持、非法地址、跨 domain
   四类结果；
7. 用 Base `PROBE_EXT` 验证“有设备/无设备”时的可见性。

不要在 vendor handler 中直接解引用 S-mode 指针。参考 DBCN 的做法，先通过 domain
检查把用户 buffer 映射为 M-mode 可安全访问的范围，再执行拷贝；否则一个错误指针
就可能让 M-mode trap 失控。

## 7. 调试一条 ecall

```gdb
b sbi_trap_handler
b sbi_ecall_handler
b sbi_ecall_find_extension
b sbi_ecall_time.c:sbi_ecall_time_handler
display/x $a7
display/x $a6
display/x $a0
```

在 `sbi_trap_handler` 记录 `mcause/mepc/mtval`；在 `sbi_ecall_handler` 记录 trap
context 中的 `a7/a6/a0-a5`；返回前确认 `mepc` 只增加 4、`a0` 是 error、`a1` 是
value。若 handler 未命中，先检查 extension 是否在 banner 的 standard/experimental
列表中，再检查 Kconfig 和 `objects.mk`。

相关文档：[architecture.md](architecture.md)、[debug-testing.md](debug-testing.md)。
