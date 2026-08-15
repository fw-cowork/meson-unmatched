# HiFive Unmatched 板级测试

`unmatched-tests.bin` 是由 U-Boot `go` 调用的目标侧测试镜像，用于验证无法在
Host 单元测试中覆盖的 FU740 时钟和 SiFive PWM 寄存器行为。

## 构建

```bash
./build.sh unmatched-tests-artifacts
```

主要产物为 `builddir/baremetal/unmatched-tests/unmatched-tests.bin`。

新开发机或 CI 先安装共享的 Freedom-U-SDK `populate_sdk` 安装器，再运行检查：

```bash
./build.sh toolchain /path/to/freedom-u-sdk-toolchain.sh
./build.sh test
```

此时产物位于 `builddir-test/baremetal/unmatched-tests/unmatched-tests.bin`。该命令会同时运行
主机算法测试，并使用 `riscv64-freedomusdk-linux-*` 检查目标 ELF；下面的 MMIO
用例仍需在真实 Unmatched 上执行。没有共享安装器时可运行 `./build.sh toolchain`
从固定源码生成同一套 SDK。

## 运行

把 BIN 放入 TFTP server root，下载一次后可以重复运行全部或单项测试：

```text
=> tftpboot 0x84000000 unmatched-tests.bin
=> go 0x84000000 all
## Application terminated, rc = 0x0

=> go 0x84000000 clock-pclk
## Application terminated, rc = 0x0

=> go 0x84000000 pwm-registers
## Application terminated, rc = 0x0
```

`clock-pclk` 读取 U-Boot 留下的 PRCI 配置，确认计算出的 PCLK 位于合理范围。
`pwm-registers` 保存 PWM0 状态，通过公共 PWM 驱动写入四个比较寄存器并读回验证，
最后无条件恢复 `PWMCFG`、`PWMCOUNT` 和 `PWMCMP0..3`。测试期间 D2 LED 可能出现
一次很短的变化；PWM1 上的风扇控制不受影响。

返回码含义：

| 返回码 | 含义 |
|---:|---|
| `0x0` | 测试通过 |
| `0x2` | 参数数量错误或参数缺失 |
| `0x3` | 未知测试名称 |
| `0x10` | PCLK 读取或计算结果异常 |
| `0x11` | PWM0 控制或比较寄存器读回失败 |

该镜像与 `baremetal/tests/` 下的 Host 单元测试互补。它需要真实 Unmatched 或等价的
硬件模型，因此不会注册为默认的 `meson test`，也不会在普通构建过程中自动运行。
