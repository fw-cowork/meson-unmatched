# OpenSBI 安全模型：特权、Domain、PMP 与共享内存

OpenSBI 不只是“把 M-mode API 转成 C 函数”。它同时是 S-mode 与 M-mode 之间的安全
边界：S-mode 可以请求服务，但不能直接改变 M-mode 的 CSR、固件内存或其它 HART 的
本地状态。本篇以当前 OpenSBI 源码为准，重点解释实际代码中的检查顺序和边界。

## 1. 信任模型

```text
硬件复位/ROM
  -> ZSBL/SPL       负责加载和交接，但不一定是长期可信运行时
  -> OpenSBI M-mode 可信计算基（trap、PMP、SBI 服务）
  -> U-Boot/Linux S-mode 受约束的调用者
  -> 用户进程 U-mode
```

这个图不是说 SPL 或 Linux 一定恶意，而是说明 OpenSBI 不能假设它们传来的参数永远
正确。尤其是：

- S-mode 传入的 buffer 地址可能越界、未对齐或跨越多个 region；
- hart mask 可能包含当前 domain 不可见的 HART；
- reset、PMU、debug trigger 可能带来不可逆的硬件副作用；
- FDT 是前级输入，必须在解析时验证节点、地址和 enabled 状态。

## 2. 三层保护机制

### 2.1 特权级和 CSR

S-mode 不能直接写 `mstatus/mepc/mtvec/pmp*`。通过 `ecall` 进入 M-mode 后，OpenSBI
在 trap context 中读取参数，再决定是否执行对应操作。`mret` 返回前重新设置
`mstatus.MPP`、`mepc` 和低级别 trap CSR。

### 2.2 Domain policy

`struct sbi_domain_memregion.flags` 同时编码 M-mode 和 S/U-mode 权限：

```text
M_READABLE / M_WRITABLE / M_EXECUTABLE
SU_READABLE / SU_WRITABLE / SU_EXECUTABLE
MMIO / FW / ENF_PERMISSIONS
```

常见区域可以画成：

| 区域 | M-mode | S/U-mode | 典型用途 |
|---|---|---|---|
| OpenSBI text | RX | 无 | 防止下一级覆盖固件代码 |
| OpenSBI data | RW | 无 | scratch、全局 runtime 状态 |
| shared console buffer | RW | RW | DBCN 等临时共享内存 |
| U-Boot/Linux RAM | 按需要 | RWX/映射策略 | 下一级程序和页表 |
| MMIO | RW | 按 region | UART、timer、PLIC、PMIC |

`sbi_domain_check_addr()` 不是 MMU 页表查找，而是根据物理 region、mode 和 access
flags 检查 capability。`sbi_domain_check_addr_range()` 会沿着 region 检查整个范围，
避免“起点合法但末尾越界”。

### 2.3 PMP/Smepmp

domain 是策略，PMP/Smepmp 是当前 HART 上的硬件实现。配置路径为：

```text
sbi_domain_finalize()
  -> sbi_hart_protection_configure()
     -> best registered protection backend
        -> PMP/Smepmp entries
```

PMP 不提供虚拟地址转换，也不替代 S-mode `satp`；它限制物理访问。没有 PMP 时，
OpenSBI 仍可运行 SBI，但不能把 M-mode firmware region 可靠地隔离出来。

## 3. 为什么保护要最后配置

`init_coldboot()` 明确把 `sbi_hart_protection_configure()` 放在 domain startup 和
所有设备/ecall 初始化之后，原因是：

1. OpenSBI 还需要访问自己的 text/data、FDT、堆和设备 MMIO；
2. platform final init 可能需要写 FDT fixup 或探测 PMIC；
3. ecall 注册必须先看到真实设备，SRST/DBCN 不能提前假设存在；
4. Smepmp 开启后，M-mode 访问 S/U space 会被撤销，过早配置会把自己锁死。

所以“PMP 最后配置”不是降低安全性，而是把初始化期和运行期的权限边界分开。

## 4. S-mode buffer 的安全访问

### 4.1 DBCN 示例

`sbi_ecall_dbcn.c` 对 console read/write 做四步检查：

```text
读取 a0=len, a1=buffer, a2=upper physical address
  -> RV32/RV64 地址高位检查
  -> sbi_domain_check_addr_range(domain, a1, len, smode, R/W)
  -> sbi_hart_protection_map_range(a1, len)
  -> sbi_nputs/ngets
  -> sbi_hart_protection_unmap_range(a1, len)
```

这里的 `a1` 不是 M-mode 可以无条件解引用的指针。OpenSBI 需要把通过 domain 检查的
物理共享范围临时映射给 M-mode，再在操作完成后撤销。

### 4.2 Smepmp 的保留 entry

当前 `sbi_hart_pmp.c` 为动态共享内存保留 PMP entry 0：

```text
启动时：entry 0 disabled
调用 DBCN/PMU/DBTR/MPXY/SSE 共享 buffer 时：临时写入 R/W region
操作完成：disable entry 0
```

这是一对必须成对出现的操作：`sbi_hart_protection_map_range()` 与
`sbi_hart_protection_unmap_range()`。新增 extension 不能只 map 不 unmap，否则后续
M-mode 会意外保留对 S/U 内存的访问。

## 5. Trap delegation 的边界

`lib/sbi/sbi_hart.c:delegate_traps()` 根据硬件能力设置 `medeleg/mideleg`。委托后，
某些异常可以直接进入 S-mode `stvec`；未委托的异常仍进入 M-mode trap handler。

阅读时要区分三种来源：

| 来源 | 默认处理位置 | 说明 |
|---|---|---|
| S-mode `ecall` | M-mode | SBI 必须接管 |
| S-mode page fault/illegal instruction | 依配置/硬件委托 | 可被下转到 S-mode |
| M timer/software/external interrupt | M-mode | OpenSBI 处理后可能置 S pending |

delegation 不是“放弃检查”：如果 trap 由 OpenSBI 先收到，`sbi_trap_redirect()` 还会
检查 previous mode、虚拟化状态和目标 `stvec`/`sepc` 语义，再决定是否下转。

## 6. 多 HART 的隔离

每个 HART 拥有独立的：

```text
mscratch -> sbi_scratch
sp       -> per-HART stack
trap context
HSM state
timer delta
IPI event bits
TLB FIFO/sync counter
```

共享对象使用 spinlock/atomic/cacheline 对齐。不能因为某个全局变量在单 HART QEMU
中工作，就认为多 HART 安全。

### 6.1 hart mask 与 domain

IPI/RFENCE/HSM handler 不能直接相信 caller 的 hart mask：

```text
caller mask
  -> hart mask/base 解析
  -> 当前 domain 的 possible/interruptible HART 集合
  -> 取交集
  -> 状态机/设备发送
```

这样一个 domain 不能控制其它 domain 的 HART，也不能让停止/刷新操作逃出授权范围。

## 7. Firmware、FDT 和 payload 的信任边界

### 7.1 FDT

FDT 不是天然可信数据库。generic 平台至少要验证：

- `/cpus` 的 hart ID、status 和数量；
- `reg/size` 是否在可访问物理地址范围；
- timer/IPI/irqchip/serial compatible 是否有对应 driver；
- `/chosen/opensbi,config` 的 heap/coldboot 参数不会越界；
- reserved-memory 不会与 firmware/scratch/next stage 重叠。

### 7.2 next stage

OpenSBI 会在 domain finalize 阶段验证 `next_addr` 对 `next_mode` 有 execute 权限，但
它不负责验证 U-Boot/Linux 镜像签名或完整性。可信启动链若需要签名校验，应由 ROM、
SPL 或单独的 verified boot 组件完成。

### 7.3 payload

`FW_PAYLOAD` 把下一级二进制内嵌到 firmware，减少前级交接复杂度，但并不自动提供
签名验证。它仍然需要正确的 payload offset、FDT offset 和 domain region。

## 8. Reset 和 debug 的特殊风险

### 8.1 SRST

system reset 是不可逆副作用。handler 必须：

1. 校验 type/reason；
2. 通过 device `system_reset_check()` 选出支持者；
3. 访问设备前验证 FDT/adapter/寄存器；
4. 尽量停止 watchdog 或按 PMIC 手册执行顺序；
5. 复位路径不能依赖会被自身 PMP 禁止的内存。

FU740 的 DA9063 driver 先 sanity check device ID，再执行 reset/shutdown；这比直接写
一个固定 I2C 地址更安全。

### 8.2 DBTR/PMU/MPXY/SSE

这些扩展都可能使用 caller 提供的共享内存或触发硬件状态。参考当前源码中的 map/
unmap 对，任何新增共享内存接口都必须覆盖：空指针、长度溢出、跨 region、错误路径
清理和并发访问。

## 9. 安全审查清单

```text
[ ] 所有 S-mode 地址先经过 domain_check_addr/range
[ ] 物理共享内存 map/unmap 成对
[ ] 失败路径也会 unmap/释放锁/清除事件位
[ ] hart mask 与当前 domain 取交集
[ ] extension 未知 function 返回 NOT_SUPPORTED
[ ] 保留参数按规范检查为零
[ ] reset/PMU/debug 等副作用有 capability 检查
[ ] FDT 地址/大小/compatible 不被直接信任
[ ] firmware region 标记 FW，next stage region 不覆盖 firmware
[ ] PMP/Smepmp 配置完成后仍能执行 mret 和必要 trap
[ ] 多 HART 使用 atomic、spinlock 和正确内存序
[ ] 不把 OpenSBI 当作 secure boot/签名验证实现
```

## 10. 安全实验建议

低风险实验顺序：

1. 在 QEMU 中让 DBCN 传入长度跨越合法 region，观察 `SBI_ERR_INVALID_PARAM`；
2. 让 `PROBE_EXT` 查询未注册 ID，确认返回 value=0 而不是 fatal trap；
3. 用无效 hart ID 调 HSM GET_STATUS，确认 domain/state 检查；
4. 在自定义测试中模拟 map 后的错误路径，确认 unmap 被调用；
5. 最后才在真实板测试 SRST，预先准备串口/JTAG 恢复路径。

相关文档：[architecture.md](architecture.md)、[source-annotations.md](source-annotations.md)、
[porting-checklist.md](porting-checklist.md)。
