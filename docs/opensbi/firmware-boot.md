# OpenSBI Firmware 启动链详解

本文只关注 firmware 入口和退出，不把所有 SBI 服务混在一起。当前 Unmatched 使用
`FW_DYNAMIC`，前级是 U-Boot SPL，后级通常是 U-Boot proper；源码路径均指向
`src/opensbi` 和 `src/u-boot`。

## 1. SPL 到 OpenSBI 的真实交接

`src/u-boot/common/spl/spl_opensbi.c` 做的事情比“跳转到一个 ELF”多：

1. 校验 FDT 非空且按 8 字节对齐；
2. 在 FIT 的 `/fit-images` 中找到 `opensbi` 后面的 OS image，得到 U-Boot entry；
3. 填充全局 `struct fw_dynamic_info opensbi_info`；
4. 使 I-cache 失效，避免刚搬移的镜像执行旧指令；
5. 多核配置下让 secondary HART 通过 `smp_call_function()` 依次进入 OpenSBI，防止
   relocation lottery 覆盖仍在运行的 SPL；
6. boot HART 调用 OpenSBI 入口：`entry(hartid, fdt, &opensbi_info)`。

本项目实际填写：

| 字段 | 值/来源 | 作用 |
|---|---|---|
| `magic` | `FW_DYNAMIC_INFO_MAGIC_VALUE = 0x4942534f` | 识别 `OSBI` 协议 |
| `version` | `2` | 启用 `boot_hart` 字段 |
| `next_addr` | FIT OS image entry | OpenSBI 最终跳转地址 |
| `next_mode` | `PRV_S` | `mret` 后进入 S-mode |
| `options` | `CONFIG_SPL_OPENSBI_SCRATCH_OPTIONS` | 早期输出/调试选项 |
| `boot_hart` | `gd->arch.boot_hart` | 首选 coldboot HART |

### 1.1 RV64 内存布局

`include/sbi/fw_dynamic.h` 使用 `__SIZEOF_LONG__` 计算 offset，所以 RV64 的结构体是
6 个 8-byte 成员，大小 48 bytes：

```text
offset 0x00  magic       0x000000004942534f
offset 0x08  version     2
offset 0x10  next_addr   U-Boot proper entry
offset 0x18  next_mode   1 (PRV_S)
offset 0x20  options     bit 0/1 等
offset 0x28  boot_hart   hart id 或 -1UL
```

不要把 magic 当成网络序列化字符串，也不要自行调整结构体字段。OpenSBI 对每个字段
使用 `assert_member_offset()`，前级和 firmware 必须同为 RV32 或同为 RV64。

## 2. `fw_base.S` 的入口阶段

不同 firmware 共用 `firmware/fw_base.S`。建议按以下标签阅读，而不是从第一条指令
线性阅读：

```text
_start
  -> fw_boot_hart()             # 选择/校验 boot HART
  -> relocate                   # PIE R_RISCV_RELATIVE 重定位
  -> _reset_regs                # 清理通用寄存器
  -> _start_hang -> mtvec       # 早期异常兜底
  -> temporary stack + clear BSS
  -> fw_save_info()             # 保存前级参数
  -> fw_platform_init(a0..a4)  # FDT 和平台实例
  -> 建立 per-HART scratch/stack/heap
  -> 可选 FDT relocation
  -> _start_warm
  -> sbi_init(mscratch)
```

### 2.1 relocation lottery

当固件使用 PIE 或前级与 OpenSBI 使用相同链接地址时，多个 HART 不能同时搬移固件。
`fw_base.S` 使用原子交换或 LR/SC 竞争 relocation lottery：一个 HART 获胜并执行
拷贝/重定位，其余 HART 等待完成后从新地址继续。

FW_DYNAMIC version 2 的 `boot_hart` 允许 SPL 指定“最后从 SPL 跳出的 HART”。这样可以
直接选定不会覆盖仍在执行 SPL 的 HART；若传 `-1UL`，OpenSBI 回退到 lottery。

重定位时重点观察：

```text
link_start = FW_TEXT_START
runtime_start = 当前 _start 地址
reloc_delta = runtime_start - link_start
R_RISCV_RELATIVE: *(where) = runtime_start + addend
```

链接脚本中的 `_fw_start` 必须紧邻 `FW_TEXT_START`，因为入口代码用它计算固件范围。
`fw_base.ldS` 将 text/rodata 与 rw/bss 分到不同区域，后续还要用于 PMP region。

## 3. 临时环境到正式 scratch

入口早期不能依赖完整 C runtime，因此先使用临时 stack 和临时 `mtvec`。完成平台
初始化后，OpenSBI 为每个 HART 建立：

```text
low memory
  firmware text/rodata
  firmware rw/data/bss
  per-HART scratch (4 KiB)
  per-HART stack
  shared heap
  relocated FDT (如需要)
high memory
  next stage (U-Boot/Linux)
```

每个 scratch 保存 `fw_start/fw_size`、`next_addr/next_mode/next_arg1`、warmboot
入口、platform 指针和 hart index。`mscratch` 指向当前 scratch，`sp` 指向当前 HART
stack；两者都由 `_start_warm` 设置后才允许进入复杂 C 代码。

FDT 不一定被复制：如果原始 FDT 位于安全且下一级可访问的 DDR，OpenSBI 可以保留原址；
若会覆盖、对齐不满足或需要扩大空闲空间，则复制到 OpenSBI 管理的地址，并通过
`next_arg1` 传给下一级。

## 4. `fw_dynamic.S` 读取协议

`fw_boot_hart()` 只使用 `a0/a1/a2`，因为它运行得非常早：

```asm
li   a1, FW_DYNAMIC_INFO_MAGIC_VALUE
REG_L a0, MAGIC(a2)
bne  a0, a1, _start_hang
li   a1, FW_DYNAMIC_INFO_VERSION_MAX
REG_L a0, VERSION(a2)
bgt  a0, a1, _start_hang
```

version >= 2 时读取 `boot_hart`；version 1 没有该字段，返回 `-1` 触发 lottery。
之后 `fw_save_info()` 将 `next_addr/next_mode/options/boot_hart` 保存到 firmware
自己的静态数据，通用 `fw_base.S` 通过 `fw_next_*()` 访问，不再依赖前级结构体地址。

这样做有两个原因：

- 前级的 scratch/栈可能被 relocation 覆盖；
- 通用流程需要在多 HART 上读取同一组启动参数，不能依赖调用者寄存器仍然保留。

## 5. coldboot 和 warmboot

进入 `sbi_init()` 后，OpenSBI 再做一次 C 层 coldboot lottery。原因是入口阶段的
“谁先跳到 firmware”与 runtime 阶段的“谁负责全局初始化”是两个问题。

### 5.1 coldboot

`init_coldboot()` 大致按这个顺序运行：

```text
初始化 scratch offset/heap
初始化 root domain 和 HART 映射
初始化 HSM，并唤醒可用的其它 HART
platform early init
HART feature、CSR delegation、PMP 能力探测
PMU、debug trigger
irqchip、IPI、TLB、timer
FWFT、MPXY，domain finalize
platform final init，SSE
注册 ecall extensions
打印 banner/domain/hart 信息
运行 SBIUnit（若 CONFIG_SBIUNIT）
domain startup
最后配置 hart protection
```

顺序不是装饰：例如 SRST 是否注册取决于平台是否已经发现 reset device；PMP 太早
配置会阻止 OpenSBI 完成自己的 FDT/设备访问。

### 5.2 warmboot

其它 HART 进入 `init_warmboot()`，等待 coldboot 完成必要的共享初始化，然后启动本
HART 的 HSM、platform、HART、PMU、irqchip、IPI、TLB、timer、FWFT、SSE，最后也走
`sbi_hsm_hart_start_finish()`。warmboot 不应重复分配全局 heap 或重新注册 ecall。

## 6. 最终 `mret`：OpenSBI 如何返回 U-Boot

`lib/sbi/sbi_hart.c:sbi_hart_switch_mode()` 做三件核心事情：

1. 检查 `next_mode` 是当前实现支持的 U/S/HS 模式；
2. 清除 `mstatus.MPP` 后写入目标模式，设置 `MPIE=0`，把 `mepc=next_addr`；
3. 为 S-mode 清理 `stvec/sscratch/sie/satp`，把 `a0=next_arg0(hartid)`、
   `a1=next_arg1(FDT)`，执行 `mret`。

所以“返回 U-Boot”不是 C 函数 return：

```text
OpenSBI M-mode
  mstatus.MPP = S
  mepc = U-Boot entry
  a0 = hartid, a1 = DTB
  mret
U-Boot S-mode 从 entry 开始执行
```

如果 `next_mode=M`，则不会获得 S-mode 隔离；如果 `next_addr` 不是有效的可执行地址，
`mret` 后立即触发 instruction access fault。

## 7. 启动故障定位表

| 现象 | 第一检查点 | 常见原因 |
|---|---|---|
| 立即停在 `_start_hang` | `a2`、magic、version | SPL 结构体地址错误、XLEN/布局不一致 |
| 多核偶发死机 | `boot_hart`、lottery、I-cache | 仍有 SPL HART 未退出、缓存未失效 |
| banner 前死机 | 临时 stack、BSS、FDT | DDR 尚未可用、FDT 地址不可读 |
| banner 后跳转异常 | `next_addr`、`next_mode`、`next_arg1` | FIT entry 错、模式值错、DTB 被覆盖 |
| 只有某个 HART 不工作 | hart map、scratch offset | DTB hart ID 非连续、映射/栈计算错误 |
| reset 返回 SBI_ENOTSUPP | ecall SRST 和 reset device | FU740 PMIC 节点未匹配、I2C 未初始化 |

## 8. 推荐断点

```gdb
b fw_boot_hart
b fw_save_info
b fw_platform_init
b _start_warm
b sbi_init
b sbi_hart_switch_mode
display/x $a0
display/x $a1
display/x $a2
```

进入 `sbi_hart_switch_mode` 时记录 `mepc`、`mstatus`、`next_mode` 和 `next_arg1`；
这是解释“为什么能回到 U-Boot”的最直接证据。

下一步阅读：[ecall-extensions.md](ecall-extensions.md)、[debug-testing.md](debug-testing.md)。
