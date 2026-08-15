# U-Boot lwIP 测试与验证

本文是独立 lwIP 端口的实际测试手册，覆盖主机编译、固件部署、静态 IPv4、DHCP、
Ping、DNS、TFTP、HTTP 和抓包验证。端口的实现契约见
[`ports/uboot-lwip/PORTING.md`](../../ports/uboot-lwip/PORTING.md)，完整移植说明见
[`lwip-port.md`](lwip-port.md)。

## 1. 测试目标

测试不只确认固件能编译，还要逐层证明以下路径有效：

```text
U-Boot command
  -> lwIP protocol state machine
  -> lwIP netif
  -> DM_ETH TX/RX
  -> PHY/switch
  -> host service
```

推荐顺序是：

1. 主机静态检查和 U-Boot 编译；
2. `lwipport info` 确认版本、命令和配置；
3. 静态 IP Ping，验证 PHY、Ethernet、ARP 和 ICMP；
4. DHCP，验证广播、定时器和环境变量写回；
5. TFTP，验证 UDP、长时间轮询和内存写入；
6. HTTP，验证 DNS、TCP、pbuf 链和文件边界；
7. 取消、超时和错误输入，验证清理路径。

QEMU 可以做主机编译和镜像结构检查，但当前目标使用 Unmatched 的 DM_ETH/PHY，
没有真实开发板时不能替代网卡收发和上板协议测试。

## 2. 测试拓扑

下面的地址与仓库其他网络文档保持一致：

| 设备 | 地址 | 说明 |
|---|---|---|
| TFTP/HTTP 主机 | `192.168.1.23/24` | 与板卡接入同一交换机 |
| HiFive Unmatched | `192.168.1.24/24` | U-Boot 中运行 `lwipport` |
| 网关 | `192.168.1.1` | 没有跨网段测试时可以省略 |
| DNS | `192.168.1.1` | 只在域名测试时需要 |

如果使用其他网段，将下文的地址统一替换。第一次测试建议直连或使用简单交换机，
暂时关闭主机防火墙对 ICMP、TFTP 和测试 HTTP 端口的阻断。

## 3. 主机准备

### 3.1 编译检查

在仓库根目录执行：

```bash
./build.sh check
./build.sh toolchain
./build.sh u-boot-lwip-port
```

主机至少需要 `make`、`bc`、`bison`、`flex`、`swig`、Python 3 和 U-Boot 所需的
开发库。交叉编译器前缀必须是：

```text
riscv64-freedomusdk-linux-
```

检查生成配置：

```bash
rg -n 'CONFIG_(CMD_LWIP_PORT|NET_LWIP|PROT_TCP|NET|LIB_RAND)' \
  out/u-boot-lwip-port/.config
```

预期至少包含：

```text
CONFIG_CMD_LWIP_PORT=y
CONFIG_NET=y
# CONFIG_NET_LWIP is not set
# CONFIG_PROT_TCP is not set
CONFIG_LIB_RAND=y
```

确认 lwIP 没有被编进 SPL：

```bash
nm out/u-boot-lwip-port/u-boot | \
  rg ' (lwip_init|httpc_get_file_dns|tftp_init_client|lwip_port_)'
nm out/u-boot-lwip-port/spl/u-boot-spl | \
  rg 'lwip_init|lwip_port_' || true
```

第一条应能看到 U-Boot proper 的 lwIP 符号，第二条不应输出任何 lwIP 符号。

### 3.2 准备测试文件

把测试文件放进 TFTP 根目录。使用固定内容可以在不同协议之间比较下载结果：

```bash
mkdir -p /srv/tftp
dd if=/dev/urandom of=/srv/tftp/lwip-test.bin bs=1024 count=64 status=progress
sha256sum /srv/tftp/lwip-test.bin
```

启动 HTTP 服务：

```bash
cd /srv/tftp
python3 -m http.server 8000 --bind 0.0.0.0
```

如果主机使用 systemd TFTP 服务，只需要确保 UDP 69 可访问；TFTP 数据阶段还会使用
动态 UDP 端口，不要只允许单个固定端口。

### 3.3 记录网络包

先在主机启动抓包，再从板卡执行命令：

```bash
sudo tcpdump -ni <host-interface> -e -vvv \
  'arp or (host 192.168.1.24 and (icmp or udp or tcp port 8000))'
```

保存串口日志和抓包文件，建议每个协议单独保存一份：

```bash
sudo tcpdump -ni <host-interface> -w lwip-ping.pcap \
  'arp or (host 192.168.1.24 and icmp)'
```

## 4. 部署固件

构建产物位于：

```text
deploy/lwip-port/u-boot-spl.bin
deploy/lwip-port/u-boot.itb
deploy/lwip-port/unmatched-firmware.itb
```

第一次测试前保留原始 SD 卡或完整镜像作为回退。将固件 FIT 复制到 TFTP 根目录：

```bash
cp deploy/lwip-port/unmatched-firmware.itb \
  /srv/tftp/unmatched-firmware-lwip.itb
chmod 0644 /srv/tftp/unmatched-firmware-lwip.itb
```

在当前可启动的 U-Boot 中更新：

```text
=> setenv firmware_fit unmatched-firmware-lwip.itb
=> run tftp_update_firmware
=> reset
```

`run tftp_update_firmware` 会写 SD 卡 loader 分区。确认目标设备、固件文件和回退
方案无误后再执行；不要把它当作普通的内存下载命令。完整恢复步骤见
[`docs/boot/tftp-boot.md`](../boot/tftp-boot.md)。

## 5. 基础命令和静态 IP

板卡启动到 U-Boot 命令行后，先确认命令已进入镜像：

```text
=> help lwipport
=> lwipport info
```

预期 `info` 输出包含类似内容：

```text
standalone lwIP port
lwIP version: 2.2.2d
source commit: 3d896ba...
execution: NO_SYS=1, polled U-Boot DM_ETH
```

没有 DHCP 时，先配置静态地址：

```text
=> setenv ipaddr 192.168.1.24
=> setenv netmask 255.255.255.0
=> setenv gatewayip 192.168.1.1
=> setenv serverip 192.168.1.23
=> setenv dnsip 192.168.1.1
=> printenv ethact ethaddr ipaddr netmask gatewayip serverip dnsip
```

### 5.1 Ping 网关或主机

```text
=> lwipport ping 192.168.1.23
=> lwipport ping 192.168.1.1
```

预期抓包顺序为 ARP request/reply，然后是 ICMP echo request/reply。也可以和 U-Boot
旧网络栈对比：

```text
=> ping ${serverip}
=> lwipport ping ${serverip}
```

两条命令都成功，说明当前 PHY、DM_ETH、ARP、IPv4、ICMP 和基本定时器路径正常。

### 5.2 测试 DNS

DNS 测试需要一个确实能从测试网络解析的主机名：

```text
=> lwipport ping <resolvable-hostname>
```

如果目标主机禁止 ICMP，Ping 失败不能单独说明 DNS 失败；可以在 HTTP 测试中使用
相同主机名，观察是否先出现 DNS 查询，再建立 TCP 连接。

## 6. DHCP 测试

静态 IP 测试通过后再测试 DHCP：

```text
=> lwipport dhcp
=> printenv ipaddr netmask gatewayip dnsip
=> lwipport ping ${serverip}
```

预期现象：

- 主机抓包看到 DHCP Discover、Offer、Request、ACK；
- U-Boot 输出 `DHCP bound`；
- `ipaddr`、`netmask`、`gatewayip` 和可用的 `dnsip` 被写回环境；
- DHCP 完成后仍能执行 `lwipport ping`。

如果 DHCP 超时，先不要调 TCP 或 HTTP；检查二层广播、交换机 VLAN、主机 DHCP 服务、
PHY link 和 `ethact`。

## 7. TFTP/UDP 测试

### 7.1 下载测试

`lwipport tftp` 的参数顺序是“目标地址、服务器地址、文件名”：

```text
=> lwipport tftp ${loadaddr} ${serverip} lwip-test.bin
=> printenv fileaddr filesize
=> crc32 ${fileaddr} ${filesize}
```

预期抓包包含 TFTP RRQ、DATA、ACK，U-Boot 输出传输字节数和耗时，`fileaddr` 等于
目标地址，`filesize` 等于主机文件大小。

### 7.2 和 legacy TFTP 对比

先用旧栈下载并记录 CRC32、文件大小，再用 lwIP 下载同一个文件：

```text
=> tftpboot ${loadaddr} ${serverip}:lwip-test.bin
=> printenv filesize
=> crc32 ${loadaddr} ${filesize}

=> lwipport tftp ${loadaddr} ${serverip} lwip-test.bin
=> printenv filesize
=> crc32 ${loadaddr} ${filesize}
```

两次文件大小和 CRC32 应一致。这个对比可以排除服务器文件变化，并验证 pbuf 复制、
UDP 回调、TFTP 重传和目标内存写入。

## 8. HTTP/TCP 测试

### 8.1 使用 IP 下载

先绕过 DNS，验证 TCP 和 HTTP：

```text
=> lwipport http ${loadaddr} http://${serverip}:8000/lwip-test.bin
=> printenv fileaddr filesize
=> crc32 ${fileaddr} ${filesize}
```

预期抓包顺序为 TCP 三次握手、HTTP GET、响应数据和连接关闭。HTTP 输出应包含
Content-Length 或实际接收长度，`filesize` 应与 TFTP 测试一致。

### 8.2 使用域名下载

```text
=> lwipport http ${loadaddr} http://<host-name>:8000/lwip-test.bin
```

这一步同时验证 DNS、TCP 和 HTTP。域名失败时先用 IP 重试：

- IP 也失败：优先检查 TCP/HTTP 服务、端口和防火墙；
- IP 成功、域名失败：优先检查 `dnsip` 和 DNS 抓包；
- 建立 TCP 但 HTTP 失败：检查 URL 格式、HTTP 状态码和 Content-Length。

当前端口只支持明文 `http://`，不支持 `https://`。

## 9. 错误路径和重复执行

每项成功测试后都应执行至少一项错误测试，确认 session 和协议 PCB 被正确释放：

```text
=> lwipport tftp ${loadaddr} ${serverip} does-not-exist.bin
=> lwipport ping 203.0.113.254
=> lwipport http ${loadaddr} http://${serverip}:8000/does-not-exist.bin
```

随后重新执行：

```text
=> lwipport ping ${serverip}
```

传输中的 TFTP 或 HTTP 可以按 Ctrl-C 取消，取消后同样重新 Ping。预期是：命令返回
失败或中断信息，但下一次 `lwipport` 能重新打开网卡并正常收发；不能出现重复回调、
持续发送旧请求或网卡无法再次初始化。

不要用 U-Boot、FDT 或保留区地址做“越界写”测试。下载地址必须使用板卡约定的
`${loadaddr}` 或已经确认可用的 LMB 区域。

## 10. 结果判定

一次完整验证至少应满足：

| 层次 | 通过条件 |
|---|---|
| 构建 | 固件生成，proper 有 lwIP 符号，SPL 无 lwIP 符号 |
| 命令 | `help lwipport` 和 `lwipport info` 正常 |
| 二层/三层 | 静态 IP Ping 成功，抓到 ARP 和 ICMP |
| DHCP | DORA 完成，环境变量正确写回 |
| DNS | 域名能解析，或 DNS 请求/响应与失败原因一致 |
| UDP | TFTP 成功，大小和 CRC32 与原文件一致 |
| TCP/HTTP | HTTP 下载成功，大小和 CRC32 与 TFTP 一致 |
| 清理 | 错误、超时、Ctrl-C 后下一次 Ping 仍成功 |

建议保存以下证据：构建日志、`lwipport info` 输出、每次下载的 `filesize`/CRC32、
串口日志和对应 `.pcap` 文件。这样后续修改 `lwipopts.h`、pbuf 池或 lwIP 版本时，
可以快速判断是构建回归、链路回归还是协议回归。

## 11. 故障定位速查

| 现象 | 优先检查 |
|---|---|
| 没有 `lwipport` 命令 | `.config` 中 `CONFIG_CMD_LWIP_PORT`、integration patch、镜像是否更新 |
| `info` 正常但没有 RX | `ethact`、PHY link、MAC、驱动 `recv/free_pkt` 生命周期 |
| ARP 有响应但 Ping 失败 | IP/掩码、`netif->output`、ICMP 回调、定时器轮询 |
| DHCP 一直超时 | 二层广播、VLAN、DHCP 服务、网卡初始化 |
| TFTP RRQ 发出但无 DATA | TFTP root、UDP 69/动态端口、防火墙、服务器文件名 |
| TFTP 成功但内容错误 | `fileaddr`/`filesize`、LMB、pbuf 链、CRC32 |
| HTTP IP 失败 | TCP 8000 服务、防火墙、HTTP URL、pbuf 池 |
| HTTP IP 成功而域名失败 | `dnsip`、DNS 响应、查询回调生命周期 |
| 取消后下一次命令失败 | PCB/pbuf 未释放、session 未 close、网卡未 halt |
