# U-Boot lwIP Porting Guide

本文面向需要修改移植层、增加协议，或把这套端口搬到另一块 U-Boot 网卡上的开发者。
它描述的是源码旁边的实现契约；构建命令、源码版本和上板操作请结合
[`docs/network/lwip-port.md`](../../docs/network/lwip-port.md) 阅读。

## 1. 端口边界

这是一套独立的、单线程的 lwIP 2.2.2d 端口。lwIP 官方源码由构建脚本固定提交并复制
到生成的 U-Boot 源码树，仓库只维护适配代码和接入补丁：

```text
U-Boot command
      |
      v
protocol module (dhcp/ping/tftp/http)
      |
      v
lwip_port_session + lwIP callback/raw API
      |
      v
lwIP netif (ARP/IP) ---- sys_now()/sys_check_timeouts()
      |
      v
U-Boot DM_ETH: eth_ops->send/recv/free_pkt
```

端口不提供 socket、netconn、线程、IPv6、TLS 或服务端接口。所有网络操作都运行在
当前 U-Boot 命令的调用栈中，由 `lwip_port_poll()` 主动驱动。

## 2. 源码职责

| 路径 | 职责 |
|---|---|
| [`u-boot-integration.patch`](u-boot-integration.patch) | 在 U-Boot 的 Kconfig、cmd、lib、net Kbuild 中注册端口 |
| [`overlay/include/lwip-port.h`](overlay/include/lwip-port.h) | 命令和应用使用的稳定公共接口，不暴露 lwIP 私有类型 |
| [`overlay/net/lwip-port/lwip-port-internal.h`](overlay/net/lwip-port/lwip-port-internal.h) | 端口内部 session、辅助声明和模块间约定 |
| [`overlay/net/lwip-port/core.c`](overlay/net/lwip-port/core.c) | netif、DM_ETH 收发、时间、DNS、环境变量 |
| `overlay/net/lwip-port/{dhcp,ping,tftp,http}.c` | 每个协议的命令级生命周期 |
| [`overlay/lib/lwip-port/port/include/lwipopts.h`](overlay/lib/lwip-port/port/include/lwipopts.h) | 功能开关、内存池和协议参数 |
| [`overlay/lib/lwip-port/port/include/arch/cc.h`](overlay/lib/lwip-port/port/include/arch/cc.h) | 编译器、整数类型、字节序和断言适配 |
| [`overlay/lib/lwip-port/Makefile`](overlay/lib/lwip-port/Makefile) | 选择官方 lwIP 源文件并加入 U-Boot 编译 |

`src/u-boot-lwip-port/` 和 `out/u-boot-lwip-port/` 是生成目录。不要在生成目录中修复
问题；修改 `overlay/` 或 integration patch 后重新运行构建。

## 3. 核心接口契约

### 3.1 打开和关闭

`lwip_port_open()` 在一次协议操作开始时调用，主要顺序如下：

1. `net_init()` 初始化 U-Boot 网卡驱动依赖的旧式报文缓冲区。
2. `eth_halt()`、`eth_set_current()`、`eth_init()` 选择并启动当前 DM_ETH 设备。
3. 检查 MAC 地址，第一次使用时调用全局 `lwip_init()`。
4. 从环境变量读取可选的 `ipaddr`、`netmask`、`gatewayip` 和 DNS 地址。
5. 使用 `netif_add()` 注册网卡，并设置默认、up 和 link-up 状态。

`use_env_address=false` 时不预置静态地址，通常交给 DHCP 模块配置。每次打开都要
检查返回值，失败时不能继续调用 poll 或协议 API。

`lwip_port_close()` 是所有退出路径的统一收尾点：先把 netif 置为 link-down/down，
调用 `netif_remove()`，停止网卡，最后清零 session。协议模块自己的 PCB、定时器或
回调状态必须在调用 close 之前释放。

### 3.2 netif 初始化

`lwip_port_netif_init()` 只做与网卡长期属性相关的设置：

- `output = etharp_output`，让 lwIP 的 ARP 层选择下一跳；
- `linkoutput = lwip_port_linkoutput`，把完整 Ethernet 帧交给 DM_ETH；
- `mtu = 1500`、硬件地址长度和 `NETIF_FLAG_*`；
- 不在这里启动 DHCP、DNS 或应用协议。

如果移植到不同 MTU 或非 Ethernet 介质，应同时检查 `PBUF_LINK_HLEN`、TCP MSS、ARP
开关和驱动对帧头的要求，而不是只改 `mtu`。

### 3.3 发送路径

`lwip_port_linkoutput()` 的输入是可能由多个 pbuf 组成的链：

1. 若 `p->next` 非空，或首地址不满足 `PKTALIGN`，使用 `memalign(PKTALIGN, p->tot_len)`
   申请连续临时缓冲区。
2. 通过 `pbuf_copy_partial()` 合并整条 pbuf 链。
3. 调用 `eth_get_ops(dev)->send(dev, packet, p->tot_len)`。
4. 发送返回后立即释放临时缓冲区，并将 U-Boot 错误转换为 `ERR_IF` 或 `ERR_MEM`。

DM_ETH 驱动只在 `send()` 调用期间读取该缓冲区，因此不能把 lwIP 的短生命周期
pbuf 指针保存到异步队列中。若未来驱动改为异步发送，应增加明确的引用计数或复制
策略，不能复用当前实现的所有权假设。

### 3.4 接收路径

`lwip_port_poll()` 是端口唯一的网络泵。每次调用按以下顺序执行：

1. `sys_check_timeouts()` 推进 ARP、DHCP、DNS、TCP 等 lwIP 定时器。
2. 调用 `schedule()`，保持 U-Boot 的协作式任务有机会运行。
3. 通过 `eth_ops->recv()` 批量取帧。
4. 为每个帧申请 `PBUF_POOL`，用 `pbuf_take()` 把驱动缓冲区复制进 pbuf 链。
5. 调用 `session->netif.input(p, &session->netif)`。
6. 无论 lwIP 是否接受该帧，都调用驱动的 `free_pkt()` 释放 U-Boot 接收缓冲区。

`netif.input()` 接管 pbuf 的所有权：成功或丢弃时都由 lwIP 协议栈负责释放。端口
不能在调用 input 后再次 `pbuf_free(p)`，否则会造成双重释放。pbuf 池耗尽时只丢弃
当前帧并继续轮询，不能阻塞等待池中出现空闲项。

`recv()` 返回无数据的驱动状态被转换为 poll 的非错误结果；其他负值应让上层退出，
并最终走 close。新增驱动适配时，先确认 `recv()` 返回长度、`packet` 生命周期和
`free_pkt()` 的错误语义，再修改这一层。

## 4. 时间、回调和内存

### 4.1 NO_SYS 事件循环

`arch/cc.h` 提供 `sys_now()`，当前实现返回 U-Boot 的毫秒计时器
`get_timer(0)`。不要在协议循环中使用忙等延时代替 poll；正确的等待方式是：

```c
while (!done) {
	if (ctrlc())
		break;
	if (lwip_port_poll(session) < 0)
		break;
}
```

DNS、DHCP、TFTP 和 HTTP 都必须在等待期间持续调用 poll，否则重传、超时和 ACK 处理
不会发生。回调参数不能指向循环体中的临时栈对象；当前 DNS 查询状态使用静态存储，
协议模块也必须保证 callback 触发前其上下文仍然有效。

### 4.2 lwIP 配置原则

当前 [`lwipopts.h`](overlay/lib/lwip-port/port/include/lwipopts.h) 的关键约束：

| 项目 | 当前值 | 原因 |
|---|---:|---|
| `NO_SYS` | `1` | U-Boot 没有给该端口提供 RTOS/线程 |
| `LWIP_NETCONN`、`LWIP_SOCKET` | `0` | 使用 callback/raw API |
| `LWIP_IPV6` | `0` | 当前板级需求只有 IPv4 |
| `MEM_LIBC_MALLOC`、`MEMP_MEM_MALLOC` | `1` | 复用 U-Boot 的 malloc/free |
| `PBUF_POOL_SIZE` | `16` | 限制轮询期间的接收内存 |
| `TCP_MSS` | `1460` | 对应标准 1500 MTU Ethernet |
| `LWIP_STATS`、`IGMP`、`SNMP`、`PPP` | `0` | 减少无用代码和静态状态 |

增大 TCP 窗口、pbuf 池或 TCP segment 数量会直接增加 U-Boot 运行时内存占用。修改
后应重新检查链接 map 和板上长时间下载，不要只依据主机编译成功判断参数合理。

### 4.3 编译器适配

`arch/cc.h` 负责把 lwIP 的 `u8_t`、`u32_t`、`LWIP_PLATFORM_DIAG`、断言、格式化
宏和字节序操作映射到 U-Boot 环境。这里的定义应保持纯编译器/libc 适配；协议开关
放到 `lwipopts.h`，网卡行为放到 `core.c`，避免头文件形成隐式平台依赖。

## 5. 协议模块生命周期

每个应用模块都遵循相同的命令级结构：打开 session，创建 lwIP 控制块，轮询直到
完成/取消/超时，释放控制块，关闭 session。当前模块的重点如下：

| 模块 | lwIP API | U-Boot 交互 |
|---|---|---|
| DHCP | `dhcp_start()` / `dhcp_supplied_address()` | 更新 `ipaddr`、`netmask`、`gatewayip`、`dnsip` |
| Ping | raw ICMP PCB | 从 `ping` 参数解析目标，匹配 echo reply 序号 |
| TFTP | upstream TFTP client | 使用 LMB 地址范围，完成后写 `fileaddr`/`filesize` |
| HTTP | upstream HTTP client | 仅支持 `http://`，正文写入 LMB，更新 `fileaddr`/`filesize` |

DHCP 在结束时要避免发送无意义的 DHCPRELEASE：地址仍会被同一次 U-Boot 会话中的
后续无状态命令复用。TFTP/HTTP 写内存前必须通过 `lmb_read_check()` 等 LMB 检查，
并使用 `map_sysmem()` 获取 CPU 可访问地址；不能直接把用户输入的物理地址强转指针。

## 6. 增加一个新协议

以新增 `foo` 命令为例，按以下顺序实现：

1. 在 `lwipopts.h` 打开所需模块，确认不会隐式拉入 socket/netconn 或线程。
2. 在 `overlay/net/lwip-port/foo.c` 创建独立的 raw/callback 状态，不把状态塞进全局
   core 结构；在 `overlay/net/lwip-port/Makefile` 加入源文件。
3. 在 `lwip-port.h` 增加最小的应用接口，在 `cmd/lwip-port.c` 添加参数解析和帮助文本。
4. 所有等待循环都处理 `ctrlc()`、`lwip_port_poll()` 失败和明确的超时；回调只访问仍
   存活的上下文。
5. 需要写内存时通过 LMB 校验和 `map_sysmem()`，需要发包时只使用 netif 的 linkoutput。
6. 为成功、协议错误、用户取消和超时分别释放 PCB、pbuf、临时缓冲区并调用 close。
7. 添加最小主机编译检查，然后在板上验证 ARP、DNS、数据传输和中断/取消路径；必要时
   用交换机镜像抓包确认 Ethernet 帧、校验和及重传。

不要在新模块中直接调用 `eth_ops->recv()` 或重新初始化 lwIP。收发和计时属于 core
端口契约，协议模块只负责协议状态机。

## 7. 错误处理约定

| 场景 | 处理 |
|---|---|
| 无网卡或无效 MAC | `open` 失败，打印设备名，调用 `eth_halt` |
| pbuf/临时 TX 缓冲不足 | 当前报文失败，返回 `-ENOMEM`/`ERR_MEM`，允许上层退出 |
| 驱动接收错误 | poll 返回负值，由命令统一 close |
| DNS/协议超时 | 返回 `-ETIMEDOUT`，不留下活动回调 |
| `ctrlc()` | 返回 `-EINTR`，先释放协议状态再 close |
| LMB 越界或保留区冲突 | 在写入前拒绝，不能部分覆盖内存 |

错误路径要和成功路径一样经过统一收尾；不能依赖下一次命令的 `lwip_init()` 来“重置”
上一次残留的 PCB。`lwip_init()` 是进程级一次性初始化，session 是命令级资源。

## 8. 构建和验证

从仓库根目录构建独立端口：

```bash
./build.sh u-boot-lwip-port
```

构建脚本会获取以下固定源码并生成独立 U-Boot 树：

- lwIP：`3d896ba0a37ff3ce73270ca5e230707fe47f60e3`（官方仓库）；
- U-Boot：由 `scripts/litebuild.py` 中的 `UBOOT_REV` 固定；
- 交叉工具链：`riscv64-freedomusdk-linux-`。

主机需要 U-Boot 的生成工具（至少 `make`、`bc`、`bison`、`flex`、`swig`）和 Python。
生成目录可以删除后重建，仓库中的 overlay、补丁和固定提交不会被修改。

板上启动后，先确认命令存在：

```text
=> lwipport info
=> printenv ethaddr ipaddr netmask gatewayip dnsip
=> lwipport dhcp
=> lwipport ping 192.168.1.1
=> lwipport ping host.example.com
=> lwipport tftp 0x88000000 192.168.1.10 image.bin
=> lwipport http 0x88000000 http://192.168.1.10/image.bin
```

验证顺序应从静态 IP 的 ping 开始，再验证 DNS、DHCP、TFTP，最后验证 HTTP。这样能
把链路、ARP、解析、UDP/TCP 和内存写入问题分层定位。

## 9. 常见故障定位

- **没有 RX 帧**：检查 `eth_init()` 后的当前设备、MAC、PHY/link 状态，以及驱动是否
  在 `free_pkt()` 前保持 `packet` 有效。
- **能看到 ARP 但 ping 不通**：检查 `netif->output` 是否为 `etharp_output`、网关/掩码
  是否正确，以及是否持续调用 `sys_check_timeouts()`。
- **DNS 一直超时**：检查 `dnsip`/`dnsip2`、UDP 53 是否可达，并确认查询回调上下文
  没有提前释放。
- **TFTP/HTTP 下载后数据错误**：检查 LMB 范围、`map_sysmem()`、目标地址对齐和
  `filesize`；不要把物理地址直接作为 C 指针。
- **重复符号或 SPL 变大**：确认 lwIP 目录只在 U-Boot proper 编译，integration patch
  中的 `CONFIG_XPL_BUILD` 保护没有被移除。

## 10. 升级 lwIP

升级时先阅读官方 [`UPGRADING`](https://github.com/lwip-tcpip/lwip/blob/master/UPGRADING)，
再在 `scripts/litebuild.py` 更新 `LWIP_REV`。随后依次检查：

1. `src/lwip/src/include/lwip/opt.h` 的新增/删除选项；
2. `src/lwip/src/core`、`api`、`netif` 的源文件清单和头文件路径；
3. `httpc`、TFTP client 等上游 API 的回调签名；
4. `cc.h`、`lwipopts.h` 的编译告警和链接大小；
5. 生成 U-Boot 的 `git apply --check`、`git diff --check`、主机编译和板上协议回归。

不要把生成的 lwIP 源码提交到仓库，也不要为了绕过升级错误在 port 层复制一份上游
实现；应修正版本钉住、源文件清单或适配代码。

更多构建流水线、目录生成关系和完整上板记录见
[`docs/network/lwip-port.md`](../../docs/network/lwip-port.md)。
