# HiFive Unmatched Lite Build Framework Design

## 1. Objective

Build a lightweight framework for studying and iterating on the HiFive
Unmatched boot chain without rebuilding a full Freedom-U-SDK Yocto image.

Target boot flow:

```text
Boot ROM
  -> U-Boot SPL
  -> OpenSBI FW_DYNAMIC
  -> U-Boot proper
  -> Linux Image.gz + hifive-unmatched-a00.dtb
```

Primary study surfaces:

```text
OpenSBI platform bring-up
U-Boot PCIe enumeration / NVMe path
Linux FU740 PCIe controller driver
Linux device tree and kernel config
```

The framework should provide repeatable, inspectable builds for these layers
with a small BusyBox root filesystem and no full user-space distribution.

## 2. Problem Statement

Freedom-U-SDK is correct for producing full board images, but it is too heavy
for day-to-day boot-chain study:

```text
Yocto parses thousands of recipes.
Full image builds pull in large user-space stacks.
Native LLVM/OpenCV/TensorFlow dependencies distract from PCIe bring-up.
do_rootfs, do_image_wic, SPDX, and SBOM stages are mostly serial.
```

For learning OpenSBI -> U-Boot -> Linux, the useful development unit is not a
distribution image. It is a set of firmware/kernel artifacts with clear
provenance and a small command surface.

## 3. Scope

### In Scope

The framework builds and deploys:

```text
fw_dynamic.bin
fw_dynamic.elf
fw_jump.bin
fw_jump.elf
fw_payload.bin
fw_payload.elf
u-boot-spl.bin
u-boot.itb
Image.gz
hifive-unmatched-a00.dtb
rootfs.ext4
unmatched-lite.img
```

It manages:

```text
source checkout
patch application
out-of-tree build directories
toolchain selection through Meson cross files
artifact deployment
local verification
optional comparison against Yocto known-good outputs
```

### Out of Scope

The framework does not build:

```text
RPM/IPK/DEB packages
full SDK sysroots
SPDX/SBOM archives
complete .wic SD-card images
large user-space stacks
```

The framework creates its own GPT image with a BusyBox ext4 rootfs. Full Yocto
images and package feeds remain outside scope.

## 4. Design Principles

### Meson Is the Orchestrator

Meson owns:

```text
configuration entry point
cross-file selection
user-facing targets
high-level dependency ordering
```

Meson does not replace:

```text
OpenSBI Makefile
U-Boot Kbuild
Linux Kbuild
```

The native build systems remain authoritative because they encode many
project-specific details that should not be duplicated.

### Cross Files Own Toolchains

Toolchain configuration must live in Meson's cross-file mechanism, not in a
pile of shell exports. The Python driver receives explicit toolchain properties
from Meson.

### The Framework Has Contracts

Each component has a contract:

```text
inputs
source revision
patch set
configure command
build command
expected artifacts
deploy names
verification checks
```

The implementation should fail early when a contract is not satisfied.

### Generated State Is Disposable

These directories are generated and ignored by git:

```text
meson-unmatched/builddir/
meson-unmatched/src/
meson-unmatched/out/
meson-unmatched/deploy/
```

Deleting them must not delete design intent or configuration.

### Optional Local Mirrors

The framework can use a local Git mirror through `UNMATCHED_LITE_GIT_CACHE`.
Without one, existing checkouts are reused and missing sources come from their
pinned upstream URLs.

## 5. Framework Architecture

Layering:

```text
User CLI
  ninja -C builddir <target>

Meson orchestration layer
  meson.build
  cross/*.ini

Framework driver layer
  scripts/litebuild.py

Component backend layer
  OpenSBI backend
  U-Boot backend
  Linux backend

External project build systems
  make / Kbuild

Artifacts
  deploy/
```

The important separation is:

```text
Meson decides what target is requested.
The driver decides how framework contracts are executed.
The upstream project decides how its own source is compiled.
```

## 6. Repository Layout

Tracked framework files:

```text
.
  README.md
  DESIGN.md
  meson.build
  .gitignore
  configs/
    linux/unmatched_defconfig
  rootfs/
    etc/inittab
    etc/init.d/rcS
  cross/
    README.md
    riscv64-linux-gnu.ini
  patches/
    u-boot/2026.01/0005-riscv-dts-Add-few-PMU-events.patch
    linux/6.18/0001-riscv-dts-sifive-unmatched-keep-leds-settings.patch
  scripts/
    litebuild.py
    fat.py

Future tracked framework files:

```text
meson-unmatched/
  manifests/
    unmatched-2026.01.json
  patches/
    u-boot/
    linux/
  scripts/
    update-boot-partition.sh
    compare-deploy.py
```

Generated files:

```text
meson-unmatched/builddir/   Meson/Ninja build directory
meson-unmatched/src/        source checkouts
meson-unmatched/out/        per-component build output
meson-unmatched/deploy/     final copied artifacts
```

## 7. Configuration Model

Configuration should be split into three layers.

### Board Manifest

The board manifest describes source-level and artifact-level facts:

```json
{
  "board": "hifive-unmatched-a00",
  "machine": "unmatched",
  "boot_flow": ["u-boot-spl", "opensbi-fw_dynamic", "u-boot-itb", "linux"],
  "components": {
    "opensbi": {
      "repo": "https://github.com/riscv/opensbi.git",
      "local_mirror": "github.com.riscv.opensbi.git",
      "revision": "74434f255873d74e56cc50aa762d1caf24c099f8",
      "platform": "generic"
    },
    "u_boot": {
      "repo": "https://source.denx.de/u-boot/u-boot.git",
      "local_mirror": "source.denx.de.u-boot.u-boot.git",
      "revision": "127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3",
      "defconfig": "sifive_unmatched_defconfig"
    },
    "linux": {
      "repo": "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git",
      "local_mirror": "git.kernel.org.pub.scm.linux.kernel.git.stable.linux.git",
      "revision": "a607c8f744340ad2c2486d46e96b66df47caffba",
      "defconfig": "meson-unmatched/configs/linux/unmatched_defconfig",
      "dtb": "sifive/hifive-unmatched-a00.dtb"
    }
  }
}
```

Initial implementation may keep this in Python constants. The framework target
is a manifest file so adding another board is not a code rewrite.

### Meson Cross File

The cross file describes toolchain facts:

```ini
[binaries]
c = 'riscv64-linux-gnu-gcc'
cpp = 'riscv64-linux-gnu-g++'
ar = 'riscv64-linux-gnu-ar'
strip = 'riscv64-linux-gnu-strip'
objcopy = 'riscv64-linux-gnu-objcopy'

[host_machine]
system = 'linux'
cpu_family = 'riscv64'
cpu = 'sifive-u740'
endian = 'little'

[properties]
cross_compile = 'riscv64-linux-gnu-'
toolchain_bindir = '/optional/path'
native_bindirs = []
```

The extra `[properties]` are framework-specific. They bridge Meson's cross
configuration into the Makefile/Kbuild world, where `CROSS_COMPILE=` is the
standard interface.

### Runtime Overrides

Environment variables are allowed only for build-local state:

```text
UNMATCHED_LITE_GIT_CACHE
UNMATCHED_LITE_SRC
UNMATCHED_LITE_OUT
UNMATCHED_LITE_DEPLOY
UNMATCHED_LITE_KEEP_CONFIG
JOBS or NINJAJOBS
```

They should not be the primary toolchain configuration mechanism.

## 8. Meson Target Model

Required user-facing targets:

```text
info
check
fetch-sources
opensbi-fw
u-boot
linux
bootchain
clean-lite
```

Optional future targets:

```text
compare-yocto
package-boot
update-boot-partition
menuconfig-linux
menuconfig-u-boot
```

`run_target()` is acceptable for the first version because OpenSBI/U-Boot/Linux
already manage incremental rebuilds internally. A later implementation can move
to `custom_target()` plus stamp files if precise Ninja artifact dependencies
become important.

Target dependency intent:

```text
fetch-sources
  -> opensbi-fw
      -> u-boot
  -> linux

bootchain = opensbi-fw + u-boot + linux
```

## 9. Component Contract

Each component backend follows this lifecycle:

```text
resolve_source()
checkout_revision()
apply_patches()
configure()
build()
deploy()
verify_artifacts()
```

Backend interface:

```text
name
source_dir
build_dir
deploy_dir
repo_url
local_mirror
revision
patches
configure_commands
build_commands
artifact_map
verification_rules
```

Artifact map example:

```text
OpenSBI:
  out/opensbi/platform/generic/firmware/fw_dynamic.bin -> deploy/fw_dynamic.bin

U-Boot:
  out/u-boot/spl/u-boot-spl.bin -> deploy/u-boot-spl.bin
  out/u-boot/u-boot.itb         -> deploy/u-boot.itb

Linux:
  out/linux/arch/riscv/boot/Image.gz                         -> deploy/Image.gz
  out/linux/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dtb -> deploy/hifive-unmatched-a00.dtb
```

Verification rules should check:

```text
file exists
file size is nonzero
file type looks correct when possible
required dependency artifact was used
```

## 10. Source Management

Source resolution order:

```text
1. Existing src/<component> git checkout
2. Optional `UNMATCHED_LITE_GIT_CACHE` mirror
3. Upstream URL
```

Default behavior for reproducibility:

```text
git reset --hard
git clean -fdx
git checkout --detach <revision>
apply framework patches
```

This makes generated output deterministic but discards uncommitted source edits
inside `src/`. For development mode, the framework should later support:

```text
UNMATCHED_LITE_DIRTY_SRC=1
```

or an explicit command-line option that skips reset/clean. Dirty-source mode
must be visible in `info` output so experiments are not mistaken for pinned
builds.

## 11. Patch Management

Patch sources are kept in the repository:

```text
U-Boot:
patches/u-boot/2026.01/0005-riscv-dts-Add-few-PMU-events.patch

Linux:
patches/linux/6.18/0001-riscv-dts-sifive-unmatched-keep-leds-settings.patch
```

Framework direction:

```text
patches/<component>/*.patch
```

Patch application contract:

```text
apply in declared order
detect already-applied patch
fail on conflict
print exact patch path
```

For learning work, patches should be small and topic-scoped:

```text
u-boot-pcie-debug-print.patch
linux-fu740-pcie-trace.patch
linux-dtb-temporary-window.patch
```

## 12. Toolchain Framework

Meson cross files are the authoritative toolchain descriptions.

Provided cross files:

```text
cross/riscv64-linux-gnu.ini
```

Preferred setup:

```bash
meson setup builddir . --cross-file cross/riscv64-linux-gnu.ini
```

SDK reuse setup:

```bash
UNMATCHED_LITE_CROSS_FILE=/path/to/toolchain.ini ./build.sh qemu
```

The Python driver receives:

```text
cross_compile
sys_root
toolchain_bindir
native_bindirs
native_sysroot_dirs
```

from Meson external properties.

### Toolchain Shim

Problem:

```text
Yocto's relocated riscv64-freedomusdk-linux-gcc may invoke unprefixed as/ld.
Outside BitBake, those names can resolve to host x86 tools.
```

Solution:

```text
out/toolchain-shim/<prefix>/
  as      -> <prefix>as
  ld      -> <prefix>ld
  objcopy -> <prefix>objcopy
```

Pass:

```text
CC="<prefix>gcc -B<shim>/"
```

This is a compatibility layer, not the preferred long-term path. The cleaner
developer setup is a normal distro or vendor RISC-V Linux toolchain.

### Native Build Tools

Required native tools:

```text
make
bc
bison
flex
m4
dtc
openssl
python3
git
mke2fs
sgdisk
```

Policy:

```text
Use system native tools when present.
Use native_bindirs from cross file as fallback.
Do not silently ignore missing tools.
```

Known prototype issue:

```text
flex-native from Yocto can fail to find m4 unless m4-native is also in PATH.
```

The Freedom-U-SDK cross file must include `m4-native/usr/bin` in
`native_bindirs`.

## 13. Build Pipelines

### OpenSBI Pipeline

Inputs:

```text
src/opensbi
CROSS_COMPILE from Meson cross file
PLATFORM=generic
FW_TEXT_START=0x80000000
```

Build:

```bash
make -C src/opensbi \
  O=out/opensbi \
  -j${JOBS} \
  REPRODUCIBLE=y \
  CROSS_COMPILE=${CROSS_COMPILE} \
  PLATFORM=generic \
  FW_TEXT_START=0x80000000
```

Deploy:

```text
fw_dynamic.bin
fw_dynamic.elf
fw_jump.bin
fw_jump.elf
fw_payload.bin
fw_payload.elf
```

Verification:

```text
fw_dynamic.bin exists and is nonzero
fw_dynamic.elf is a RISC-V ELF
```

### U-Boot Pipeline

Inputs:

```text
src/u-boot
deploy/fw_dynamic.bin
sifive_unmatched_defconfig
patches/u-boot/2026.01/0005-riscv-dts-Add-few-PMU-events.patch
```

Configure:

```bash
make -C src/u-boot \
  O=out/u-boot \
  ARCH=riscv \
  CROSS_COMPILE=${CROSS_COMPILE} \
  sifive_unmatched_defconfig
```

Build:

```bash
make -C src/u-boot \
  O=out/u-boot \
  ARCH=riscv \
  CROSS_COMPILE=${CROSS_COMPILE} \
  OPENSBI=deploy/fw_dynamic.bin \
  -j${JOBS}
```

Deploy:

```text
u-boot-spl.bin
u-boot.itb
```

Verification:

```text
u-boot-spl.bin exists and is nonzero
u-boot.itb exists and is nonzero
mkimage/dumpimage can inspect u-boot.itb when available
```

### Linux Pipeline

Inputs:

```text
src/linux
configs/linux/unmatched_defconfig
patches/linux/6.18/0001-riscv-dts-sifive-unmatched-keep-leds-settings.patch
```

Configure:

```bash
cp configs/linux/unmatched_defconfig out/linux/.config
make -C src/linux \
  O=out/linux \
  ARCH=riscv \
  CROSS_COMPILE=${CROSS_COMPILE} \
  olddefconfig
```

Build:

```bash
make -C src/linux \
  O=out/linux \
  ARCH=riscv \
  CROSS_COMPILE=${CROSS_COMPILE} \
  -j${JOBS} \
  Image.gz dtbs
```

Deploy:

```text
Image.gz
hifive-unmatched-a00.dtb
```

Verification:

```text
Image.gz exists and is nonzero
hifive-unmatched-a00.dtb exists and is nonzero
dtc can decompile the DTB when available
```

### BusyBox Rootfs Pipeline

Inputs:

```text
BusyBox 1.37.0 source tarball
rootfs/ overlay
cross-file toolchain and sysroot
```

Build:

```bash
ninja -C meson-unmatched/builddir rootfs
```

The driver builds a static BusyBox binary, installs its applet links into an
out-of-tree root directory, copies the tracked init scripts, and creates an
ext4 image with `mke2fs -d`. No mount, fakeroot, package manager, or Yocto WIC
template is required.

## 14. Artifact Contract

Deploy directory is the framework API:

```text
meson-unmatched/deploy/
```

Stable artifact names:

```text
fw_dynamic.bin
u-boot-spl.bin
u-boot.itb
Image.gz
hifive-unmatched-a00.dtb
busybox
rootfs.ext4
unmatched-lite.img
```

Debug/secondary artifacts:

```text
fw_dynamic.elf
fw_jump.*
fw_payload.*
```

The framework should also produce a future metadata file:

```json
{
  "board": "hifive-unmatched-a00",
  "toolchain": "riscv64-freedomusdk-linux-",
  "components": {
    "opensbi": {
      "revision": "74434f...",
      "artifacts": ["fw_dynamic.bin"]
    }
  }
}
```

This allows later comparison and boot logging without guessing which artifact
came from which source.

## 15. Boot Media Strategy

The framework creates a complete GPT image with fixed Unmatched partition
offsets and a BusyBox ext4 rootfs:

```text
SPL raw region -> U-Boot ITB raw region -> FAT boot -> ext4 BusyBox rootfs
```

The build never writes removable media. Flashing remains an explicit separate
operation after the image has been inspected.

Do not hide media writes inside normal build targets. Any command that writes
to `/dev/sdX` must be explicit and separate.

Future helper contract:

```bash
scripts/update-boot-partition.sh /dev/sdX
```

It should:

```text
print lsblk summary
require explicit device argument
mount only the boot partition
copy Image.gz and hifive-unmatched-a00.dtb
optionally copy u-boot.itb only when requested
sync and unmount
```

## 16. PCIe Study Workflow

U-Boot PCIe loop:

```text
edit U-Boot PCIe/NVMe code
ninja -C builddir u-boot
deploy u-boot.itb
boot to U-Boot prompt
run pci enum / pci 0 / nvme scan
record serial log
```

Linux PCIe loop:

```text
edit Linux pcie-fu740.c or DTS
ninja -C builddir linux
deploy Image.gz + DTB
boot Linux
check dmesg, lspci, /sys/bus/pci/devices
```

OpenSBI loop:

```text
edit OpenSBI platform/PMU/domain code
ninja -C builddir opensbi-fw
ninja -C builddir u-boot
deploy u-boot.itb
boot and inspect SBI/U-Boot/Linux behavior
```

## 17. Diagnostics and Logging

The driver should print:

```text
component name
source directory
build directory
revision
patch list
cross_compile value
effective CC
important command lines
deployed artifacts
```

Failures should include:

```text
missing tool name
missing source mirror or failed upstream URL
patch conflict path
failed command
expected artifact path
suggested next command when obvious
```

Avoid silent fallback that changes semantics. Falling back from local mirror to
network should be printed clearly.

## 18. Verification Matrix

Local checks:

```text
check             host tools and cross tools visible
fetch-sources     all source trees at expected commits
opensbi-fw        fw_dynamic.bin generated
u-boot            u-boot.itb generated using deploy/fw_dynamic.bin
linux             Image.gz and DTB generated
```

Artifact checks:

```text
file size > 0
ELF class/arch for .elf files
FIT image inspection for u-boot.itb when mkimage/dumpimage exists
DTB decompile for hifive-unmatched-a00.dtb when dtc exists
```

Board checks:

```text
U-Boot:
  version
  pci enum
  pci 0
  nvme scan
  nvme info

Linux:
  uname -a
  dmesg | grep -iE 'sbi|pci|pcie|nvme'
  lspci -vv
  cat /proc/iomem
  ls /sys/bus/pci/devices
```

Regression checks against Yocto deploy:

```text
compare file presence
compare artifact type
compare DTB compatible strings
compare kernel config PCIe symbols
```

## 19. Error Handling Policy

Framework errors should be actionable:

```text
Bad cross file:
  print missing property and example cross file path.

Missing host tool:
  print tool name and whether system install or native_bindirs can satisfy it.

Patch failed:
  print component, patch, source revision, and stop.

Artifact missing:
  print expected path and build command that should have produced it.

Dirty source mode:
  require explicit opt-in before preserving local source edits.
```

The framework should not run destructive source cleanup in dirty-source mode.

## 20. Extension Points

New board:

```text
add manifest
add cross file if needed
add board-specific patches
add artifact map
```

New component:

```text
add backend contract
add source entry
add build pipeline
add deploy artifacts
add verification rules
```

Possible future components:

```text
initramfs
device-tree overlays
OpenOCD config
boot partition packager
```

## 21. Implementation Phases

### Phase 1: Framework Skeleton

Deliver:

```text
DESIGN.md
README.md
meson.build
cross files
Python driver
source fetch
check target
OpenSBI build
```

Exit criteria:

```text
ninja -C builddir check passes
ninja -C builddir opensbi-fw creates fw_dynamic.bin
```

### Phase 2: Boot Chain Build

Deliver:

```text
U-Boot build
Linux build
artifact verification helpers
better native tool handling
deploy metadata JSON
```

Exit criteria:

```text
u-boot.itb exists
Image.gz exists
hifive-unmatched-a00.dtb exists
all artifacts have metadata
```

### Phase 3: Board Iteration Tools

Deliver:

```text
boot partition update helper
Yocto artifact comparison helper
PCIe debug workflow notes
optional menuconfig targets
```

Exit criteria:

```text
new kernel/DTB can be deployed onto a known-good SD card
board-side PCIe checklist is documented
```

## 22. Open Questions

1. Should source dirty mode be supported in phase 2, or should all experiments
   be patches first?
2. Should an initramfs be added alongside the ext4 BusyBox rootfs?
3. Should `u-boot.itb` be deployed by replacing the raw U-Boot partition, by
   TFTP, or both?
4. Should Linux modules be built later, or is built-in PCIe/NVMe enough for the
   study workflow?
