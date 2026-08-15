# SBI ABI 速查

这篇是查表用的 companion 文档。规范解释“接口应该是什么”，本地源码解释“当前
OpenSBI 如何实现”；两者不一致时，以固定源码对应的 SBI 规范版本和头文件为准。

## 1. 一次调用的完整寄存器协议

```text
调用前：
  a7 = extension ID
  a6 = function ID
  a0-a5 = function arguments
  ecall

调用后：
  a0 = error (0 或负数)
  a1 = value（legacy v0.1 不保证）
  mepc = 原 mepc + 4
```

`ecall` 是 32-bit 指令，OpenSBI 在 `lib/sbi/sbi_ecall.c` 中统一增加 4。caller 不应
自己修改返回 PC，也不应把 `a0` 当作无符号长度或普通 C 返回值。

### 1.1 XLEN 差异

| 情况 | RV64 | RV32 |
|---|---|---|
| 普通 `ulong` 参数 | 一个寄存器 | 一个寄存器 |
| 64-bit timer | `a0` | `a1:a0`，低 32 位在 a0 |
| 64-bit 地址 | 一个寄存器 | 按规范拆成参数，必须看具体 extension |
| `fw_dynamic_info` | 每字段 8 bytes | 每字段 4 bytes |

不要把“ABI 的 XLEN 拆分”与 C 编译器如何传 `uint64_t` 混为一谈；SBI handler 直接
读取保存于 `struct sbi_trap_regs` 的寄存器。

## 2. Error code

定义在 `include/sbi/sbi_ecall_interface.h`，别名在 `include/sbi/sbi_error.h`：

| 数值 | 名称 | 典型含义 |
|---:|---|---|
| `0` | `SBI_SUCCESS` | 调用成功 |
| `-1` | `SBI_ERR_FAILED` | 未分类失败 |
| `-2` | `SBI_ERR_NOT_SUPPORTED` | extension/function/能力不存在 |
| `-3` | `SBI_ERR_INVALID_PARAM` | 参数值或组合非法 |
| `-4` | `SBI_ERR_DENIED` | 当前 domain/权限不允许 |
| `-5` | `SBI_ERR_INVALID_ADDRESS` | 地址不合法或不可访问 |
| `-6` | `SBI_ERR_ALREADY_AVAILABLE` | 资源已可用/已配置 |
| `-7` | `SBI_ERR_ALREADY_STARTED` | HART/资源已经启动 |
| `-8` | `SBI_ERR_ALREADY_STOPPED` | HART/资源已经停止 |
| `-9` | `SBI_ERR_NO_SHMEM` | 未设置共享内存 |
| `-10` | `SBI_ERR_INVALID_STATE` | 状态机当前不能执行 |
| `-11` | `SBI_ERR_BAD_RANGE` | 范围不能表达或越界 |
| `-12` | `SBI_ERR_TIMEOUT` | 等待超时 |
| `-13` | `SBI_ERR_IO` | 设备 I/O 错误 |
| `-14` | `SBI_ERR_DENIED_LOCKED` | 已锁定，不能再次修改 |

OpenSBI 内部还使用 `SBI_ENODEV`、`SBI_ENOMEM` 等实现内部错误。它们通常在初始化
阶段被消费，不应作为公开 SBI ABI 重新发给 S-mode caller。

## 3. Base extension (`0x10`)

| function | 输入 | 返回 `a0/a1` | 本地实现 |
|---|---|---|---|
| `GET_SPEC_VERSION=0` | 无 | `0 / (major << 24 \| minor)` | `sbi_ecall_base.c` |
| `GET_IMP_ID=1` | 无 | `0 / implementation ID` | OpenSBI ID 为 1 |
| `GET_IMP_VERSION=2` | 无 | `0 / OPENSBI_VERSION` | release version |
| `PROBE_EXT=3` | `a0=extid` | `0 / 0 or 1` | 查注册链表 |
| `GET_MVENDORID=4` | 无 | `0 / mvendorid` | 读取 CSR |
| `GET_MARCHID=5` | 无 | `0 / marchid` | 读取 CSR |
| `GET_MIMPID=6` | 无 | `0 / mimpid` | 读取 CSR |

S-mode 启动时建议先调用 `GET_SPEC_VERSION` 和 `PROBE_EXT`，再决定是否使用 TIME、
HSM、SRST 等服务。不要把启动 banner 中的 extension 字符串当成稳定 API；程序应
使用 `PROBE_EXT`。

## 4. 当前项目最常用扩展

### 4.1 TIME (`0x54494d45`)

```text
a6 = 0 (SET_TIMER)
RV64: a0 = absolute time compare value
RV32: a1:a0 = absolute 64-bit compare value
```

实现：`sbi_ecall_time.c -> sbi_timer_event_start()`。有 Sstc 时写 `stimecmp`，否则
调用平台 timer device。传入的是绝对计数值，不是“延迟多少 tick”。

### 4.2 IPI (`0x735049`)

```text
a6 = 0 (SEND_IPI)
a0 = hart mask
a1 = hart mask base
```

`hmask=0` 且 `hbase != -1` 表示没有目标，不是广播。handler 会把目标与当前 domain
的 interruptible HART 集合相交；目标不属于 domain 时返回错误。

### 4.3 RFENCE (`0x52464e43`)

所有 RFENCE function 都使用 `a0=hart mask`、`a1=hart mask base`，其余参数如下：

| function | `a2` | `a3` | `a4` |
|---|---|---|---|
| `REMOTE_FENCE_I` | 保留 | 保留 | 保留 |
| `REMOTE_SFENCE_VMA` | start | size | 保留 |
| `REMOTE_SFENCE_VMA_ASID` | start | size | ASID |
| `REMOTE_HFENCE_GVMA_VMID` | start | size | VMID |
| `REMOTE_HFENCE_GVMA` | start | size | 保留 |
| `REMOTE_HFENCE_VVMA_ASID` | start | size | ASID |
| `REMOTE_HFENCE_VVMA` | start | size | 保留 |

`size=0`/`start=0` 和实现定义的 `SBI_TLB_FLUSH_ALL` 表示全量刷新；最终是否按范围
刷新由 `sbi_tlb.c` 和平台的 `get_tlbr_flush_limit` 决定。

### 4.4 HSM (`0x48534d`)

| function | 输入 | 返回 value |
|---|---|---|
| `HART_START=0` | `a0=hartid, a1=start_addr, a2=opaque` | 无；成功 error=0 |
| `HART_STOP=1` | 无 | 无；当前 HART 停止 |
| `HART_GET_STATUS=2` | `a0=hartid` | HSM 状态枚举 |
| `HART_SUSPEND=3` | `a0=suspend_type, a1=resume_addr, a2=opaque` | 视类型而定 |

启动目标入口要符合 SBI HSM 约定：目标 HART 以指定模式进入，不能假设旧的 `satp`、
`stvec` 或 S-mode 中断状态仍然有效。

### 4.5 SRST (`0x53525354`)

```text
a6 = 0 (RESET)
a0 = type: 0 shutdown, 1 cold reboot, 2 warm reboot
a1 = reason: 0 none, 1 system failure
```

当前 FU740 的 `sbi_ecall_srst.c` 先验证 type/reason，再查询 system reset device。
没有匹配到 DA9063 或其它 reset device 时，该 extension 可能不注册；这不是 handler
忽略调用，而是 capability 没有出现。

## 5. 其它 extension 的函数速查

| Extension | function IDs | 关键参数/用途 |
|---|---|---|
| DBCN `0x4442434e` | `WRITE=0`, `READ=1`, `WRITE_BYTE=2` | `a0=len`, `a1=buffer`, `a2=upper address`; 先做 domain range check |
| PMU `0x504d55` | `0..8` | counter 数量、配置、启停、读取和 snapshot |
| SUSP `0x53555350` | `SUSPEND=0` | `a0=sleep type`, `a1=resume addr`, `a2=opaque` |
| FWFT `0x46574654` | `SET=0`, `GET=1` | `a0=feature`, `a1=value/flags`, `a2=flags` |
| CPPC `0x43505043` | `PROBE=0`, `READ=1`, `READ_HI=2`, `WRITE=3` | `a0=register ID` |
| DBTR `0x44425452` | `0..7` | debug trigger 数量、共享内存、安装/更新/启停 |
| SSE `0x535345` | `0..9` | event 属性、注册、启停、注入、hart mask |
| MPXY `0x4d505859` | `0..7` | shared memory、channel、message proxy |

具体结构体、共享内存布局和 feature ID 不应从这张表推导，直接看
`include/sbi/sbi_ecall_interface.h` 与对应 handler。

## 6. 从 C/汇编 caller 发起调用

裸机程序通常用下面的寄存器约定；示例只展示约定，不建议复制成没有 clobber 约束的
内联汇编：

```asm
li   a7, SBI_EXT_BASE
li   a6, SBI_EXT_BASE_GET_SPEC_VERSION
ecall
mv   t0, a0       # error
mv   t1, a1       # value
```

在 C 中优先使用项目已有的 SBI wrapper；如果必须写 inline asm：

1. 明确把输入绑定到 `a0..a7`；
2. 把 `a0/a1` 声明为读写输出；
3. 加 `memory` clobber，防止 buffer 操作被重排；
4. 不把 `t0-t6`、`a0-a7` 的副作用隐藏在普通 C 函数声明里；
5. RV32/RV64 分别测试 64-bit 参数。

## 7. 版本兼容原则

| 做法 | 评价 |
|---|---|
| 直接假设 SRST 可用 | 错误；先 `PROBE_EXT`，再处理 `NOT_SUPPORTED` |
| 只判断 `a0 == 0`，忽略 `a1` | 错误；HSM status、Base probe 等结果在 `a1` |
| 固定使用 legacy ID 0/4/5 | 仅兼容旧 caller；新程序使用现代 extension |
| 未检查保留参数是否为零 | 可能被未来实现拒绝，应按规范清零 |
| 将 S-mode 虚拟地址直接交给 M-mode | 错误；DBCN/PMU 等共享 buffer 要做 domain/physical 检查 |

关联文档：[ecall-extensions.md](ecall-extensions.md)、[architecture.md](architecture.md)。
