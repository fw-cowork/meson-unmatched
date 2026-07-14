# 构建说明

本仓库默认构建 SiFive HiFive Unmatched 物理板的 SD 卡镜像。QEMU 使用独立
profile，不模拟 FU740 ROM 和物理板的 SPL 装载过程。

首次构建前，请按 `README.md` 的 Dependencies 章节安装宿主工具、KAS 和
QEMU。

## 初始化 SiFive 工具链

首次执行时下载 SiFive Freedom-U-SDK `2026.01.00` 的源码和 Yocto 依赖，生成
Linux SDK 并安装到本仓库的 `toolchains/sifive/`：

```bash
cd /home/adrian/devel/riscv/meson-unmatched
./build.sh toolchain
./build.sh check
```

该步骤需要较大的磁盘空间和较长时间。工具链生成完成后，后续普通构建不再依赖
父目录中的 KAS 工程。

## Unmatched 物理板

不带参数时构建物理板 SD 卡镜像：

```bash
./build.sh
```

构建过程会自动下载固定版本的 OpenSBI、U-Boot、Linux 和 BusyBox，并生成：

```text
deploy/unmatched-lite.img
```

镜像包含以下启动链：

```text
U-Boot SPL -> OpenSBI FW_DYNAMIC -> U-Boot -> Linux Image.gz + DTB -> BusyBox rootfs
```

## QEMU

QEMU profile 独立输出到 `deploy/qemu/`，使用 8 个 CPU 和 2 GiB 内存：

```bash
./build.sh qemu
./qemu.sh
```

也可以由启动脚本负责构建：

```bash
./qemu.sh --build
```

自动验证启动时使用超时参数：

```bash
./qemu.sh --timeout 24
```

QEMU 的启动链为：

```text
OpenSBI FW_DYNAMIC -> QEMU S-mode U-Boot -> boot.scr -> FIT -> Linux -> BusyBox
```

`deploy/qemu/fit.itb` 包含内核、QEMU virt DTB 和 BusyBox CPIO rootfs；
`deploy/qemu/qemu-lite.img` 的 FAT 分区只包含 FIT 和 `boot.scr`。

### FIT 格式

FIT 是 Flattened Image Tree。它将多个启动组件和它们的元数据放入单个
`fit.itb` 文件中。当前仓库只有 QEMU profile 使用 FIT；物理 Unmatched 镜像仍
使用独立的 `Image.gz`、DTB、extlinux 配置和 ext4 rootfs 分区。

QEMU 构建会生成 `out/qemu/fit.its`，再使用本次构建的 U-Boot `mkimage` 工具
生成 `deploy/qemu/fit.itb`。FIT 的默认配置名为 `qemu`，包含：

```text
kernel   deploy/qemu/Image.gz          Linux 内核，加载和入口地址为 0x80200000
fdt      out/qemu/qemu-virt.dtb        与当前 8 CPU、2 GiB QEMU virt 配置匹配的 DTB
ramdisk  deploy/qemu/rootfs.cpio.gz    BusyBox initramfs
```

每个 FIT 子镜像都有 SHA-256 哈希。U-Boot 在加载时验证哈希，任意一个校验失败
都会停止启动。`rootfs.cpio.gz` 在 FIT 中作为未再压缩的数据传给 Linux，由内核
处理其 gzip CPIO 格式。

`boot.scr` 的实际启动逻辑为：

```text
fatload virtio 0:1 0x84000000 fit.itb
setenv bootargs console=ttyS0 earlycon=sbi
bootm 0x84000000#qemu
```

因此 U-Boot 读取单个 FIT，并根据 `qemu` 配置同时取得内核、DTB 和 initramfs。
检查 FIT 内容：

```bash
out/qemu/u-boot/tools/mkimage -l deploy/qemu/fit.itb
```

## QEMU GDB 调试

启动 QEMU 并在复位处暂停：

```bash
./qemu-gdb.sh --build
```

在另一个终端连接：

```bash
gdb-multiarch deploy/qemu/fw_dynamic.elf
```

```gdb
target remote 127.0.0.1:1234
continue
```

默认 GDB server 只监听 `127.0.0.1:1234`。使用其他端口：

```bash
./qemu-gdb.sh --port 1235
```

## 清理

删除构建和部署产物，同时保留已下载源码：

```bash
ninja -C builddir clean-lite
```

需要重新下载所有组件时，再手动删除 `src/`、`downloads/` 或
`toolchains/sifive/`。
