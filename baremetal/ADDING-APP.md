# 添加 bare-metal 测试程序

本文说明如何在本项目中新增一个由 U-Boot `go` 启动的 HiFive Unmatched 测试程序。
应用只需要提供 C 入口和 Meson 清单；公共启动汇编、链接脚本、编译选项及 ELF/BIN
产物规则由 `baremetal/` 统一管理。

以下示例创建名为 `my-test` 的应用。

## 1. 创建应用目录

```bash
mkdir -p baremetal/apps/my-test
```

目录最终应包含：

```text
baremetal/apps/my-test/
├── main.c       应用入口和测试逻辑
├── meson.build  源文件及公共组件依赖
└── README.md    板端命令、预期输出和返回码（建议添加）
```

应用名应使用小写字母、数字和连字符。它同时决定 Meson 目标名、生成文件名和产物目录名。

## 2. 实现入口函数

创建 `baremetal/apps/my-test/main.c`：

```c
/* SPDX-License-Identifier: MIT */

#include <baremetal/app.h>
#include <drivers/sifive_uart.h>
#include <soc/fu740.h>

enum my_test_result {
	MY_TEST_PASS = 0,
	MY_TEST_INVALID_ARGUMENTS = 2,
};

bm_ulong baremetal_main(int argc, char *const argv[])
{
	const bm_ulong uart = FU740_CONSOLE_UART_BASE;

	/* U-Boot go 把入口地址作为 argv[0]，无额外参数时 argc 必须为 1。 */
	if (!argv || argc != 1 || !argv[0])
		return MY_TEST_INVALID_ARGUMENTS;

	/* go 应用复用 U-Boot 已初始化的 UART0，不需要重新设置波特率。 */
	sifive_uart_puts(uart, "my-test: PASS\n");

	/* 返回 0 后，start.S 会恢复现场并返回 U-Boot 命令行。 */
	return MY_TEST_PASS;
}
```

入口函数签名必须保持为：

```c
bm_ulong baremetal_main(int argc, char *const argv[]);
```

不要自行定义 `_start`。公共
[`arch/riscv/cpu/start.S`](arch/riscv/cpu/start.S) 会保存 U-Boot 返回地址、清零
`.bss`、调用 `baremetal_main()`，然后把返回值交还给 U-Boot。

## 3. 声明应用依赖

创建 `baremetal/apps/my-test/meson.build`：

```meson
baremetal_apps += [{
  'name': 'my-test',
  'sources': files('main.c'),
  'dependencies': [baremetal_sifive_uart_dep],
}]
```

若应用有多个私有源文件，可以全部列入 `sources`：

```meson
'sources': files('main.c', 'device_test.c'),
```

按实际使用情况选择公共依赖：

| Meson 依赖 | 提供的能力 |
|---|---|
| `baremetal_string_dep` | `bm_streq()` 等无 libc 字符串函数 |
| `baremetal_fu740_dep` | FU740 PCLK 计算 |
| `baremetal_sifive_uart_dep` | SiFive UART 轮询收发和十六进制输出 |
| `baremetal_sifive_pwm_dep` | SiFive PWM 计算和寄存器配置 |

只登记程序真正使用的依赖。公共头文件和 U-Boot `go` 架构入口由顶层规则自动加入，
无需在每个应用中重复声明。

### 选择是否返回 U-Boot

省略 `runtime` 时默认使用 `return`：入口沿用 U-Boot 栈，`baremetal_main()` 的返回值
成为 U-Boot 显示的 `rc`。

需要切换到私有栈并永久接管当前 hart 时，显式选择 `standalone`：

```meson
baremetal_apps += [{
  'name': 'my-test',
  'runtime': 'standalone',
  'sources': files('main.c'),
  'dependencies': [baremetal_sifive_uart_dep],
}]
```

standalone runtime 使用独立的 `start-standalone.S` 和 `u-boot-go-standalone.lds`，把
`0x8400c000..0x84010000` 保留为 16 KiB 私有栈。即使 `baremetal_main()` 意外返回，
入口也会永久循环，不会返回 U-Boot。完整示例见
[unmatched-standalone](apps/unmatched-standalone/README.md)。

## 4. 注册应用目录

在 [`apps/meson.build`](apps/meson.build) 末尾添加：

```meson
subdir('my-test')
```

这一步使顶层 `baremetal/meson.build` 能读取应用清单，并自动创建该应用的所有产物目标。

## 5. 构建和检查

只构建这个应用的完整分析产物：

```bash
./build.sh my-test-artifacts
```

只需要上板使用的 BIN 和镜像检查结果时：

```bash
./build.sh my-test-bin
```

成功后生成：

```text
builddir/baremetal/my-test/
├── my-test.elf    带调试信息的 ELF
├── my-test.bin    U-Boot tftpboot 加载的原始镜像
├── my-test.map    链接映射
├── my-test.dis    带源码的反汇编
├── my-test.sym    按地址排序的符号表
└── my-test.check  入口地址和重定位检查结果
```

先查看检查结果：

```bash
cat builddir/baremetal/my-test/my-test.check
```

合格镜像必须满足以下条件：

- ELF 入口和 `_start` 都是 `0x84000000`；
- ELF 中没有运行时重定位；
- `return` runtime 的 64 KiB 镜像检查通过，或 `standalone` runtime 的 48 KiB
  代码/数据/BSS与 16 KiB 栈边界检查通过；
- 链接时没有 libc、OpenSBI 或 U-Boot 私有符号依赖。

## 6. 通过 TFTP 上板运行

把 `builddir/baremetal/my-test/my-test.bin` 放入 TFTP 服务器目录，然后在 U-Boot 中执行：

```text
=> tftpboot 0x84000000 my-test.bin
=> go 0x84000000
## Starting application at 0x84000000 ...
my-test: PASS
## Application terminated, rc = 0x0
```

当前镜像固定链接到 `0x84000000`。`tftpboot` 的目标地址和 `go` 的入口地址都必须使用
该地址；`go` 不会解析 ELF、重定位代码或初始化新的运行环境。

传递额外参数时：

```text
=> go 0x84000000 hello
```

应用收到的参数为：

```text
argc = 2
argv[0] = "0x84000000"
argv[1] = "hello"
```

因此解析命令时应从 `argv[1]` 开始。返回 `0` 表示测试通过；非零返回值会作为 U-Boot
显示的 `rc`，应用 README 应记录每个返回码的含义。

## 7. 编写测试逻辑时的约束

- 程序运行在调用它的 U-Boot 当前特权级：普通 U-Boot 下通常是 S-mode，M-mode U-Boot
  下才是 M-mode。
- 可以复用 U-Boot 已初始化的 DDR、栈、UART、时钟和引脚复用，但不能假设存在操作系统
  或 hosted C 运行时。
- 不要使用 `printf()`、`malloc()` 等 libc API；优先使用 `baremetal/include/` 中的接口。
- 修改 PWM、GPIO 等外设后，即使程序返回 U-Boot，寄存器状态也不会自动恢复。
- 修改页表、trap 入口、关键 CSR、中断或时钟时，必须在返回前恢复，否则 U-Boot 可能无法
  继续运行。无法恢复的测试应在 README 中明确要求执行 `reset`。
- 不要覆盖 U-Boot 的栈、程序镜像之外的未知 DDR 区域或 `0x84000000` 上的自身代码。
- standalone 应用不会回到 U-Boot，必须在 README 中明确复位方式和预期最后一行输出。

## 8. 何时抽取公共代码

仅由 `my-test` 使用的代码继续放在 `baremetal/apps/my-test/`。当第二个应用也需要相同
功能时，再根据职责移动到：

| 目录 | 适合的代码 |
|---|---|
| `baremetal/lib/` | 不访问硬件的通用算法和工具 |
| `baremetal/soc/fu740/` | FU740 地址空间、时钟等 SoC 专属逻辑 |
| `baremetal/drivers/<ip>/` | 不包含板级映射的可复用 IP 驱动 |
| `baremetal/tests/` | 可由主机编译器运行的纯算法单元测试 |

公共组件应通过 `declare_dependency()` 导出，由应用的 `dependencies` 显式选择。不要在
应用目录复制 `start.S`、链接脚本或现有驱动。

## 9. 提交前检查

```bash
./build.sh my-test-artifacts
./build.sh baremetal-test
git diff --check
```

最后在真实 Unmatched 上记录 TFTP 下载字节数、串口输出和 U-Boot `rc`。MMIO、特权级
CSR 及真实外设行为无法仅靠主机单元测试验证。
