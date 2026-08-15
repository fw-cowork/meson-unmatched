# 在 U-Boot 上从零移植最新 lwIP

本文记录本仓库的独立 lwIP 移植过程。它不是打开 U-Boot 2026.01 自带的
`CONFIG_NET_LWIP`，而是把官方最新源码作为外部组件，自己完成编译配置、平台适配、
网卡收发和应用命令。

如果要修改端口实现或增加协议，请先看源码旁的
[`ports/uboot-lwip/PORTING.md`](../../ports/uboot-lwip/PORTING.md)。那份文档集中说明
`netif`/DM_ETH 接口契约、pbuf 所有权、NO_SYS 轮询和协议模块生命周期；本文侧重构建
流水线、版本固定和上板验证。

## 1. 目标与边界

构建固定到 lwIP 官方提交
`3d896ba0a37ff3ce73270ca5e230707fe47f60e3`，该提交的版本字符串是
`2.2.2d`。固定提交而不是跟随 `master`，可以保证另一台机器得到相同源码。

本次“完整移植”指打通一个可工作的 U-Boot 单线程端口：

- IPv4、Ethernet、ARP、ICMP、DHCP、DNS、UDP 和 TCP；
- TFTP 与明文 HTTP 下载；
- U-Boot DM_ETH 网卡驱动、环境变量、计时器、内存和命令行适配；
- 可独立生成 SPL、U-Boot FIT 和固件升级 FIT。

它不表示打开 lwIP 的全部模块。当前没有 OS 线程、socket/netconn、IPv6、HTTPS、
FTP、PPP 或服务端功能。先把最小闭环做清楚，比同时移植线程和 TLS 更适合学习。

## 2. 两条 lwIP 路径

| 构建目标 | lwIP 来源 | 网络栈关系 | 用途 |
|---|---|---|---|
| `u-boot-lwip` | U-Boot 2026.01 内置版本 | 替换 legacy `CONFIG_NET` | 验证上游集成 |
| `u-boot-lwip-port` | lwIP 官方仓库固定提交 | 与 legacy Ping/TFTP 共存 | 学习独立移植 |

独立端口使用命令名 `lwipport`，所以可以在同一固件中对比 `ping` 与
`lwipport ping`、`tftpboot` 与 `lwipport tftp`。U-Boot legacy TCP 和 lwIP 都导出
`tcp_init()`，因此该目标关闭 `CONFIG_PROT_TCP` 和 legacy `wget`；HTTP 由新端口提供。

## 3. 源码与生成目录

仓库只保存适配代码和 U-Boot 接入补丁，不复制第三方源码：

```text
ports/uboot-lwip/
├── u-boot-integration.patch       # Kconfig 和 Kbuild 接入点
└── overlay/
    ├── cmd/lwip-port.c            # U-Boot 命令解析
    ├── include/lwip-port.h        # 不暴露 lwIP 类型的公共接口
    ├── lib/lwip-port/
    │   ├── Makefile               # 选择要编译的官方 lwIP 源文件
    │   └── port/include/
    │       ├── lwipopts.h         # 功能、内存和协议参数
    │       └── arch/cc.h          # 编译器/libc 平台定义
    └── net/lwip-port/
        ├── core.c                 # netif、TX/RX、时钟、DNS
        ├── dhcp.c
        ├── ping.c
        ├── tftp.c
        └── http.c
```

构建时 [litebuild.py](../../scripts/litebuild.py) 执行以下流水线：

```text
官方 lwIP 固定提交 ─┐
                    ├─> src/u-boot-lwip-port/ ─> out/u-boot-lwip-port/
U-Boot 固定提交 ────┤       ^
板级补丁 + overlay ─┘       └─ 生成版本头并复制 lwIP src/
```

`src/u-boot-lwip-port/` 是每次重建的生成树。不要直接修改它；应修改
`ports/uboot-lwip/overlay/` 或接入补丁。

## 4. 第一步：确定运行模型

U-Boot 没有供 lwIP 使用的常驻网络线程，因此配置为：

```c
#define NO_SYS               1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN         0
#define LWIP_SOCKET          0
```

这决定了后续设计：应用使用 callback/raw API，命令循环主动调用
`lwip_port_poll()`。它依次执行：

1. `sys_check_timeouts()` 推进 ARP、DHCP、DNS 和 TCP 定时器；
2. `schedule()` 让 U-Boot 其他协作任务运行；
3. 调用 DM_ETH 驱动的 `recv()`；
4. 把收到的帧复制进 pbuf，并交给 `netif_input()`。

如果只完成收发而忘记 `sys_check_timeouts()`，ARP 表会失效，DHCP 重传、DNS 超时
和 TCP 重传也都不会发生。这是 NO_SYS 移植最常见的问题之一。

## 5. 第二步：适配编译器、libc 和内存

[`arch/cc.h`](../../ports/uboot-lwip/overlay/lib/lwip-port/port/include/arch/cc.h)
告诉 lwIP 如何使用 U-Boot 的整数类型、`errno`、随机数、断言和字节序函数。
[`lwipopts.h`](../../ports/uboot-lwip/overlay/lib/lwip-port/port/include/lwipopts.h)
使用 U-Boot 的 `malloc/free`：

```c
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT   8
```

FU740 是 RV64，8 字节对齐与 ABI 一致。pbuf 池仍保留 16 个接收缓冲，避免每个 RX
包都直接依赖堆分配。TCP MSS 设为 1460，对应 1500 字节 Ethernet MTU 扣除
IPv4/TCP 头。

## 6. 第三步：实现 netif

[`core.c`](../../ports/uboot-lwip/overlay/net/lwip-port/core.c) 是移植核心。

发送方向：

```text
lwIP pbuf chain -> 必要时拼成 PKTALIGN 对齐的连续缓冲 -> DM_ETH send()
```

不能假设 `p->len == p->tot_len`。TCP 或应用层可能产生 pbuf 链，而网卡驱动要求一段
连续内存；因此代码用 `pbuf_copy_partial()` 合并整条链，并按 `PKTALIGN` 分配。

接收方向：

```text
DM_ETH recv() -> pbuf_alloc(PBUF_POOL) -> pbuf_take()
              -> netif_input() -> DM_ETH free_pkt()
```

复制看似多一次开销，但它明确分离了网卡 DMA 缓冲和 lwIP pbuf 的所有权。只有在
驱动与 lwIP 能共同管理引用计数时，才适合进一步做零拷贝。

`lwip_port_open()` 从当前 `ethact` 设备读取 MAC，并从 `ipaddr`、`netmask`、
`gatewayip` 初始化静态地址。DHCP 命令则以全零地址创建 netif。`sys_now()` 直接使用
U-Boot 的毫秒计时器 `get_timer(0)`。

## 7. 第四步：逐个打通协议

推荐按以下顺序上板，不要一开始就调 HTTP：

1. `lwipport info`：确认新命令和固定版本进入镜像；
2. DHCP：验证广播 TX/RX、定时器和环境变量写回；
3. Ping：验证 ARP、IPv4、校验和与 raw callback；
4. TFTP：验证 UDP、长时间轮询和内存写入；
5. HTTP：验证 DNS、TCP 状态机、pbuf 链和 Content-Length。

协议文件只依赖 `lwip-port-internal.h` 提供的四个原语：打开、关闭、轮询和解析
地址。命令层只看到 [`lwip-port.h`](../../ports/uboot-lwip/overlay/include/lwip-port.h)，
不会把 lwIP 类型扩散到 U-Boot CLI。

## 8. 构建与换机复现

准备仓库固定的 `riscv64-freedomusdk-linux-` 工具链后执行：

```bash
./build.sh toolchain
./build.sh u-boot-lwip-port
```

主机还需要 U-Boot 的构建依赖，至少包括 `make`、`bc`、`bison`、`flex`、`swig`、
OpenSSL/GnuTLS 开发文件和 Python 3。它们是主机工具，不属于 RISC-V 交叉编译器。
`./build.sh check` 会在正式构建前报告缺项。

产物不会覆盖默认固件：

```text
out/u-boot-lwip-port/
deploy/lwip-port/u-boot-spl.bin
deploy/lwip-port/u-boot.itb
deploy/lwip-port/unmatched-firmware.itb
```

构建脚本优先从现有 `src/u-boot` 创建共享对象克隆；新机器没有该目录时会从官方
U-Boot 仓库获取。lwIP 始终校验并切换到固定提交。

## 9. 上板测试

可直接照着执行的测试步骤、预期抓包和验收标准见
[`lwip-test.md`](lwip-test.md)。本节保留最小的快速验证命令。

先保留可重新写卡的默认固件，再通过现有固件升级流程写入
`deploy/lwip-port/unmatched-firmware.itb`。启动后执行：

```text
=> lwipport info
=> lwipport dhcp
=> printenv ipaddr netmask gatewayip serverip dnsip
=> lwipport ping ${serverip}
=> lwipport tftp ${loadaddr} ${serverip} test.bin
=> lwipport http ${loadaddr} http://192.168.1.23/test.bin
=> printenv fileaddr filesize
```

没有 DHCP 时可先设置静态地址：

```text
=> setenv ipaddr 192.168.1.24
=> setenv netmask 255.255.255.0
=> setenv gatewayip 192.168.1.1
=> setenv serverip 192.168.1.23
=> setenv dnsip 192.168.1.1
```

抓包时按 ARP request/reply、ICMP echo、DHCP DORA、TFTP RRQ/DATA/ACK、TCP
三次握手和 HTTP GET 的顺序检查。若 DHCP 成功而 HTTP 域名失败，优先检查
`dnsip`；若只发送不接收，优先检查 `ethact`、PHY link 和 RX buffer 所有权。

下载命令通过 U-Boot LMB 检查目标地址，仍应使用板卡约定的 `loadaddr`，不要写入
U-Boot、FDT 或保留内存。

## 10. 升级 lwIP

升级不是把 `master` 浮动地留给每次构建。正确步骤是：

1. 阅读 [lwIP 官方仓库](https://github.com/lwip-tcpip/lwip)中的
   [`UPGRADING`](https://github.com/lwip-tcpip/lwip/blob/master/UPGRADING)；
2. 更新 `scripts/litebuild.py` 中的 `LWIP_REV`；
3. 运行 `./build.sh u-boot-lwip-port` 做全量编译；
4. 用 `lwipport info` 核对版本与提交；
5. 依次回归 DHCP、Ping、TFTP、HTTP，并保存抓包结果。

这样每次 API 或默认配置变化都能在端口边界被发现，而不会悄悄改变其他机器的构建。
