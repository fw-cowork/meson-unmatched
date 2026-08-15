# Unmatched Lite Framework Specification

This document specifies the lightweight framework used to study the HiFive
Unmatched boot chain:

```text
OpenSBI -> U-Boot PCIe -> Linux PCIe
```

The goal is not to design another distribution build system. The framework
builds and verifies the firmware/kernel artifacts needed for board iteration,
while a known-good Freedom-U-SDK SD image supplies the root filesystem.

## 1. Architecture Boundary

The framework is split into five explicit surfaces:

```text
configuration surface   boards/ manifests/ schemas/ cross/
source surface          ignored source trees and local mirrors
execution surface       Meson targets and Python scripts
artifact surface        deploy/ manifest.json logs
operator surface        docs/ lab/ sd-update tools
```

Rules:

```text
configuration files describe intent
scripts implement intent
Meson schedules commands
Ninja executes commands
deploy/ records evidence
SD-card writes require an explicit operator command
```

The framework must not silently fetch, patch, deploy, or write removable media
from unrelated targets.

## 2. Format Policy

Native file formats:

```text
YAML       board, manifest, artifact, and boot-flow configuration
Meson INI  cross compilation toolchain files
JSON       generated deploy metadata
PlantUML   architecture and process diagrams
Markdown   human documentation
```

YAML is used for hand-written framework configuration because nested board,
component, artifact, and boot-flow data stays compact and readable. Framework
scripts must parse YAML through a structured parser and validate schemas before
using the data.

## 3. Repository Contracts

Tracked source of truth:

```text
boards/
manifests/
schemas/
cross/
patches/
scripts/
docs/
tests/
3rdparty/
meson-unmatched/
```

Ignored generated state:

```text
sifiveinc-2026.01/
downloads/
toolchains/
meson-unmatched/src/
meson-unmatched/out/
meson-unmatched/deploy/
meson-unmatched/builddir/
*.wic
*.wic.xz
*.ext4
```

Large upstream source trees must be referenced by manifests and checked out to
ignored directories. They must not be vendored into git.

## 4. Schema Model

Framework YAML files must be validated before execution.

Schema layout:

```text
schemas/board.schema.json
schemas/manifest.schema.json
schemas/boot-flow.schema.json
schemas/artifacts.schema.json
```

Validation pipeline:

```text
read YAML
reject duplicate keys if parser supports it
convert to JSON-compatible data
validate with JSON Schema
run semantic checks that require cross-file knowledge
```

Static schema checks:

```text
required fields
field type
allowed enum values
artifact entry shape
component backend id
path string format
```

Semantic checks:

```text
manifest board must match selected board
component dependencies must reference known components or artifacts
patch paths must stay under patches/
artifact deploy names must be unique
required boot-flow artifacts must be produced by a component
toolchain properties must exist in the selected Meson cross file
```

Schema files are framework-owned. External schema files, if needed, belong
under `3rdparty/schemas/` with provenance notes.

## 5. Configuration Model

### 5.1 Board File

Path:

```text
boards/hifive-unmatched-a00/board.yml
```

Required fields:

```yaml
board: hifive-unmatched-a00
vendor: sifive
soc: fu740
arch: riscv64
machine: unmatched
console:
  baud: 115200
  linux: ttySIF0
boot_media:
  type: sd
  partition_table: gpt
```

Semantics:

```text
board       stable framework board id
vendor      board vendor
soc         SoC family
arch        target CPU architecture
machine     upstream machine or board name
console     serial settings used in logs and docs
boot_media  physical boot media class, not a device path
```

No host-specific paths are allowed in `board.yml`.

### 5.2 Manifest File

Path:

```text
manifests/unmatched-lite.yml
```

Required fields:

```yaml
name: unmatched-lite
board: hifive-unmatched-a00
source_policy:
  network: explicit
  dirty_tree: reject
components:
  opensbi:
    repo: https://github.com/riscv-software-src/opensbi.git
    mirror: github.com.riscv-software-src.opensbi.git
    revision: 74434f255873d74e56cc50aa762d1caf24c099f8
    backend: opensbi-make
    artifacts:
      - from: platform/generic/firmware/fw_dynamic.bin
        to: fw_dynamic.bin
        verifier: nonempty
        required: true
```

Component fields:

```text
repo        canonical upstream URL
mirror      local mirror directory name
revision    pinned git revision, tag, or release commit
backend     framework backend id
defconfig   optional backend-specific default config
patches     optional ordered patch series
artifacts   declared outputs copied into deploy/
depends     optional dependency artifact list
```

`revision` must be resolved to a full commit hash in generated deploy metadata,
even if the manifest uses a tag for readability.

### 5.3 Boot Flow File

Path:

```text
boards/hifive-unmatched-a00/boot-flow.yml
```

Example:

```yaml
stages:
  - name: opensbi
    provides:
      - fw_dynamic.bin
  - name: u-boot
    consumes:
      - fw_dynamic.bin
    provides:
      - u-boot-spl.bin
      - u-boot.itb
  - name: linux
    consumes:
      - hifive-unmatched-a00.dtb
    provides:
      - Image.gz
```

This file documents dependency order for humans and allows future verification
to check that required artifacts are produced before they are consumed.

### 5.4 Artifact Placement File

Path:

```text
boards/hifive-unmatched-a00/artifacts.yml
```

Example:

```yaml
boot_partition:
  filesystem: vfat
  files:
    - artifact: u-boot-spl.bin
      required: true
    - artifact: u-boot.itb
      required: true
    - artifact: Image.gz
      required: true
    - artifact: hifive-unmatched-a00.dtb
      required: true
```

This file describes where framework artifacts belong. It must not contain a
host device path such as `/dev/sdX`.

## 6. Dependency Graph

The framework should build an explicit dependency graph before running any
component target.

Graph nodes:

```text
component:opensbi
component:u-boot
component:linux
artifact:fw_dynamic.bin
artifact:u-boot-spl.bin
artifact:u-boot.itb
artifact:Image.gz
artifact:hifive-unmatched-a00.dtb
```

Graph edges:

```text
component produces artifact
component consumes artifact
component depends on component
boot-flow stage orders artifacts
```

Rules:

```text
cycles are configuration errors
missing producers are configuration errors
optional artifacts are excluded from required boot validation
component build order is topological order
parallel builds are allowed only between independent subgraphs
```

For the initial Unmatched flow the graph is intentionally mostly linear:

```text
opensbi -> fw_dynamic.bin -> u-boot -> u-boot-spl.bin/u-boot.itb
linux -> Image.gz/hifive-unmatched-a00.dtb
```

Linux does not consume U-Boot as a build input, but the SD boot set consumes
both U-Boot and Linux artifacts.

## 7. Command Model

Meson/Ninja is the primary build entry:

```bash
meson setup builddir meson-unmatched --cross-file cross/sifive-freedom-u-sdk.ini
ninja -C builddir info
ninja -C builddir fetch
ninja -C builddir opensbi-fw
ninja -C builddir u-boot
ninja -C builddir linux
ninja -C builddir deploy
ninja -C builddir verify
```

Operator wrapper:

```bash
./scripts/unmatched info
./scripts/unmatched fetch
./scripts/unmatched build opensbi
./scripts/unmatched build u-boot
./scripts/unmatched build linux
./scripts/unmatched deploy
./scripts/unmatched verify
./scripts/unmatched sd-update --device /dev/sdX --boot-only
```

Wrapper rules:

```text
must call Meson/Ninja for builds
must print selected board, manifest, cross file, and deploy path
must support dry-run for mutating commands
must require explicit --device for removable media writes
must fail if required deploy artifacts are missing or invalid
```

CLI grammar:

```text
unmatched [global-options] <command> [command-options]

global-options:
  --board <id>
  --manifest <path>
  --cross-file <path>
  --builddir <path>
  --jobs <n>
  --verbose
  --dry-run

commands:
  info
  fetch [component]
  build <component|all>
  deploy [component|all]
  verify [component|all]
  clean <component|all>
  sd-update --device <path> --boot-only
```

Command behavior:

```text
info       no mutation, prints resolved configuration and paths
fetch      may access network only when source_policy allows it
build      compiles, may create out/ and logs/, does not update SD media
deploy     copies built artifacts into deploy/
verify     validates deploy artifacts and manifest consistency
clean      removes generated output only for selected component
sd-update  verifies deploy/ first, then updates the boot partition
```

## 8. Component State Machine

Every component moves through the same lifecycle:

```text
declared -> fetched -> checked_out -> patched -> configured -> built -> deployed -> verified
```

State files:

```text
out/<component>/.state/source.json
out/<component>/.state/patch.json
out/<component>/.state/build.json
deploy/manifest.json
```

Rules:

```text
state is generated and ignored by git
state is used for diagnostics, not as the source of truth
dirty source trees block checkout unless explicitly allowed
patch failures stop before configure
verification failures stop before SD-card update
```

## 9. Backend Interface

A backend is the framework adapter for an upstream build system.

Backend ids:

```text
opensbi-make
uboot-kbuild
linux-kbuild
copy-only
```

Required backend operations:

```text
probe       check required host tools and input artifacts
configure   prepare out-of-tree build directory
build       compile the component
deploy      copy declared artifacts
verify      run artifact-specific checks
clean       remove generated output for one component
```

Common backend context:

```text
workspace_root
board_id
manifest_path
cross_file
cross_compile
toolchain_bindir
native_bindirs
source_dir
out_dir
deploy_dir
jobs
verbose
dry_run
```

Backends must not read host-specific global config. All required paths must come
from Meson cross properties, manifest files, or command-line arguments.

Backend module layout:

```text
scripts/unmatched
scripts/unmatchedlib/
  config.py
  graph.py
  runner.py
  backends/
    opensbi_make.py
    uboot_kbuild.py
    linux_kbuild.py
    copy_only.py
  verify/
    artifacts.py
    elf.py
    dtb.py
    fit.py
```

Backends should return structured results:

```text
phase
component
command
returncode
artifacts
log_path
diagnostics
```

The command wrapper formats results for humans. Tests can assert on structured
results without parsing terminal output.

## 10. Source Management

Source policy values:

```text
network = "explicit"   fetch only when fetch target or wrapper fetch is called
network = "never"      only use existing local source/mirror
dirty_tree = "reject"  fail if source tree has uncommitted changes
dirty_tree = "allow"   allow local experiments and record dirty state
```

Checkout layout:

```text
meson-unmatched/src/opensbi/
meson-unmatched/src/u-boot/
meson-unmatched/src/linux/
```

Build layout:

```text
meson-unmatched/out/opensbi/
meson-unmatched/out/u-boot/
meson-unmatched/out/linux/
```

The framework may reuse local Freedom-U-SDK mirrors to avoid network fetches,
but the selected upstream revision still belongs in `manifests/`.

## 11. Patch Management

Patch layout:

```text
patches/opensbi/<version>/
patches/u-boot/<version>/
patches/linux/<version>/
```

Manifest patch declaration:

```yaml
patches:
  - patches/u-boot/2026.01/0001-riscv-dts-add-fu740-pmu-events.patch
```

Rules:

```text
patch order is manifest order
directory order is not authoritative
debug patches live under a named topic directory
temporary patches must be marked in the patch subject or manifest comment
patch application writes a patch log
```

Experiments should happen in ignored source trees first. Once useful, they
should be promoted into patches and referenced by the manifest.

## 12. Artifact Verification

Verifier ids:

```text
exists
nonempty
elf-riscv
binary-nonzero
fit-image
dtb-readable
gzip-kernel
text-contains
```

Verifier output:

```text
artifact name
source path
deployed path
size
sha256
verifier id
result
diagnostic message
```

Deploy manifest:

```json
{
  "board": "hifive-unmatched-a00",
  "manifest": "unmatched-lite.yml",
  "cross_file": "sifive-freedom-u-sdk.ini",
  "components": {
    "u-boot": {
      "revision": "127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3",
      "dirty": false,
      "artifacts": [
        {
          "name": "u-boot.itb",
          "sha256": "...",
          "size": 123456,
          "verifier": "fit-image"
        }
      ]
    }
  }
}
```

The SD-card update command must verify `deploy/manifest.json` before copying
files.

## 13. Deploy Layout

Deploy output should be stable and inspectable:

```text
meson-unmatched/deploy/
  current -> runs/<deploy-id>/
  runs/
    20260707-153000-unmatched-lite/
      manifest.json
      artifacts/
        fw_dynamic.bin
        u-boot-spl.bin
        u-boot.itb
        Image.gz
        hifive-unmatched-a00.dtb
      logs/
        build-opensbi.log
        build-u-boot.log
        build-linux.log
        verify.log
```

Rules:

```text
current is updated only after verification succeeds
failed deploys keep logs but do not become current
artifact checksums are recorded before current is updated
sd-update defaults to deploy/current
older deploy runs are never deleted by build targets
```

This layout lets the board user roll back to the last known-good artifact set
without rebuilding.

## 14. Parallelism Policy

Parallelism is explicit:

```text
component build jobs use -j from Meson/Ninja or --jobs
independent verification can run in parallel
source fetch can run in parallel only for independent components
SD-card writes are never parallel
deploy/manifest.json writes are serialized
```

Default job count:

```text
use user-provided --jobs first
else use NINJA_STATUS/Ninja job server if available
else use host CPU count
```

The framework should print the selected job count before long builds.

## 15. Logging and Diagnostics

Log layout:

```text
logs/<timestamp>/setup.log
logs/<timestamp>/fetch-opensbi.log
logs/<timestamp>/build-u-boot.log
logs/<timestamp>/verify.log
```

Every failure message should include:

```text
component
phase
command
working directory
log path
next diagnostic command
```

Example:

```text
component: u-boot
phase: configure
failed command: make sifive_unmatched_defconfig
log: logs/20260707-153000/build-u-boot.log
hint: check CROSS_COMPILE and native tool paths from the selected cross file
```

## 16. Error Codes

Stable error classes:

```text
E_CONFIG      invalid YAML, missing field, unsupported backend
E_TOOLCHAIN   compiler, binutils, or native helper missing
E_SOURCE      clone, checkout, dirty tree, or revision failure
E_PATCH       patch application failure
E_BUILD       upstream build command failure
E_ARTIFACT    missing or invalid artifact
E_DEPLOY      deploy copy or manifest write failure
E_MEDIA       SD-card device, mount, copy, sync, or unmount failure
E_SCHEMA      YAML parsed but failed framework schema validation
E_GRAPH       component or artifact dependency graph is invalid
```

Scripts should return nonzero on failure and include the error class in the
final diagnostic line.

## 17. SD-Card Update Transaction

`sd-update` is a transaction with explicit preflight and rollback boundaries.

Preflight:

```text
device path exists
device is removable or explicitly forced
device is not the host root disk
deploy/current exists
deploy manifest verifies
boot partition can be identified
required artifacts are present
```

Transaction:

```text
mount boot partition read-write
copy existing files to backup directory on host
copy new files to a temporary directory on the boot partition
fsync files and directory
rename temporary files into final names
sync filesystem
unmount partition
```

Failure policy:

```text
fail before mount if preflight fails
fail before mutation if deploy verification fails
if copy fails, leave original files untouched where possible
if rename fails, report exact files that changed
never format unless --format is passed and confirmed
```

The command must print the device, partition, source deploy id, and file list
before mutation. `--dry-run` must show the same plan without mounting or
copying.

## 18. Safety Rules

Destructive or host-specific operations require explicit input:

```text
SD-card writes require --device
partition formatting requires --format and an interactive confirmation
source reset requires --reset-source
dirty tree override requires --allow-dirty
network fetch requires fetch target or --fetch
```

No normal build target may format, mount, or write a removable block device.

## 19. Test Strategy

Default tests must not require network or board access.

Test layers:

```text
schema tests       valid and invalid YAML fixtures
graph tests        dependency order, missing producer, cycle detection
backend tests      command generation with dry-run runners
verifier tests     tiny ELF/DTB/FIT/gzip fixtures where practical
deploy tests       manifest generation and current symlink behavior
safety tests       sd-update preflight with fake block-device metadata
```

Board tests are opt-in:

```text
serial boot capture
U-Boot PCIe enumeration
Linux lspci output
NVMe or endpoint probe
PCIe error counter checks
```

## 20. Minimal Implementation Plan

Phase 1:

```text
create boards/ and manifests/ YAML files
move cross files to top-level cross/
teach litebuild.py to read YAML
generate deploy/manifest.json
```

Phase 2:

```text
split backends from litebuild.py
add artifact verifier
add framework wrapper scripts/unmatched
add dry-run mode
```

Phase 3:

```text
add SD boot partition updater
add serial log capture helper
add PCIe check recipes
add tests for YAML parsing and command generation
```

Phase 4:

```text
support additional boards only after Unmatched is stable
add optional rootfs integration if the learning workflow requires it
```
