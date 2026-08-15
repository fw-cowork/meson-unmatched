# Unmatched Study Repository Framework

## 1. Goal

This repository should become a small, clean framework for studying SiFive
HiFive Unmatched bring-up:

```text
OpenSBI -> U-Boot PCIe -> Linux PCIe
```

It should not become a second Yocto distribution. The full Freedom-U-SDK tree
is retained as a known-good reference and artifact provider, while this repo
owns the lightweight learning workflow, manifests, patches, scripts,
documentation, and validation.

UML diagrams are kept as PlantUML source under `docs/uml/`. They describe
architecture and flow; they do not replace YAML manifests or Meson cross files.

## 2. References

The structure is intentionally influenced by mature embedded repositories:

```text
Zephyr:
  boards/ samples/ scripts/ tests/ doc/ split board support, examples,
  automation, validation, and documentation.

Buildroot:
  board/ configs/ package/ support/ toolchain/ split board policy,
  defconfigs, packages, helper tooling, and toolchain description.

U-Boot:
  arch/ board/ configs/ drivers/ doc/ tools/ split architecture code,
  board support, defconfigs, drivers, docs, and host tools.

OpenSBI:
  platform/ firmware/ lib/ include/ docs/ scripts/ split platform support,
  firmware payloads, common library code, public interfaces, documentation,
  and automation.
```

Source repositories:

```text
https://github.com/zephyrproject-rtos/zephyr
https://github.com/buildroot/buildroot
https://github.com/u-boot/u-boot
https://github.com/riscv-software-src/opensbi
```

## 3. Design Direction

The repository should be organized around framework responsibilities, not
around one-off scripts.

Core responsibilities:

```text
describe boards
pin upstream source versions
describe toolchains through Meson cross files
fetch or reuse source mirrors
apply patches
build components
deploy artifacts
verify artifacts
document workflows
record board experiments
```

Non-responsibilities:

```text
build a full rootfs
build arbitrary user-space packages
replace Yocto package management
hide writes to removable media inside normal build targets
vendor large upstream source trees into git
```

## 4. Target Repository Layout

Proposed top-level layout:

```text
.
├── README.md
├── build.md
├── docs/
│   ├── repo-framework.md
│   ├── framework-spec.md
│   ├── pcie-study.md
│   ├── uml/
│   │   ├── README.md
│   │   ├── repository-components.puml
│   │   ├── build-sequence.puml
│   │   ├── data-model.puml
│   │   ├── artifact-flow.puml
│   │   ├── component-state.puml
│   │   ├── command-surface.puml
│   │   ├── dependency-graph.puml
│   │   └── sd-update-sequence.puml
│   ├── bringup/
│   │   ├── boot-flow.md
│   │   ├── serial-console.md
│   │   └── sd-card.md
│   ├── pcie/
│   │   ├── u-boot-pcie.md
│   │   ├── linux-pcie.md
│   │   └── debug-checklist.md
│   └── decisions/
│       ├── 0001-use-meson-orchestration.md
│       ├── 0002-keep-rootfs-out-of-lite-build.md
│       └── 0003-use-meson-cross-files.md
├── boards/
│   └── hifive-unmatched-a00/
│       ├── board.yml
│       ├── boot-flow.yml
│       ├── artifacts.yml
│       ├── pcie-checks.md
│       └── README.md
├── manifests/
│   ├── freedom-u-sdk-2026.01.yml
│   └── unmatched-lite.yml
├── schemas/
│   ├── board.schema.json
│   ├── manifest.schema.json
│   ├── boot-flow.schema.json
│   └── artifacts.schema.json
├── cross/
│   └── sifive-freedom-u-sdk.ini
├── 3rdparty/
│   ├── README.md
│   ├── schemas/
│   └── tools/
├── patches/
│   ├── opensbi/
│   ├── u-boot/
│   │   └── 2026.01/
│   └── linux/
│       └── 6.18/
├── scripts/
│   ├── repo-info.py
│   ├── fetch-sources.py
│   ├── build-component.py
│   ├── deploy-artifacts.py
│   ├── verify-artifacts.py
│   ├── compare-yocto.py
│   └── update-boot-partition.sh
├── meson-unmatched/
│   ├── README.md
│   ├── DESIGN.md
│   ├── meson.build
│   └── scripts/
│       └── litebuild.py
├── lab/
│   └── unmatched.sh
├── tests/
│   ├── manifests/
│   ├── scripts/
│   └── artifacts/
└── sifiveinc-2026.01/
    └── ignored external SDK/build tree
```

This is a target framework. The current repository can migrate toward it in
small steps without breaking the already-built SDK image.

## 5. Directory Responsibilities

## 5.1 `boards/`

Board-owned facts live here.

Example:

```text
boards/hifive-unmatched-a00/
  board.yml
  boot-flow.yml
  artifacts.yml
  pcie-checks.md
```

Responsibilities:

```text
identify board and SoC
name UART console
name expected DTB
describe boot sequence
describe PCIe validation commands
declare artifact names expected by boot media
```

This mirrors the board-centric organization seen in Zephyr, Buildroot, and
U-Boot, but keeps this repository focused on one board first.

Example `board.yml`:

```yaml
board: hifive-unmatched-a00
vendor: sifive
soc: fu740
machine: unmatched
serial:
  baud: 115200
  linux_console: ttySIF0
kernel:
  image: Image.gz
  dtb: hifive-unmatched-a00.dtb
u_boot:
  defconfig: sifive_unmatched_defconfig
opensbi:
  platform: generic
```

## 5.2 `manifests/`

Manifests describe source provenance and component contracts.

Responsibilities:

```text
pin upstream repos and commits
map local Yocto mirrors
declare patch series
declare build backend
declare output artifact contracts
```

Example `unmatched-lite.yml`:

```yaml
components:
  opensbi:
    repo: https://github.com/riscv-software-src/opensbi.git
    mirror: github.com.riscv-software-src.opensbi.git
    revision: 74434f255873d74e56cc50aa762d1caf24c099f8
    backend: opensbi-make
    artifacts:
      - from: platform/generic/firmware/fw_dynamic.bin
        to: fw_dynamic.bin

  u_boot:
    repo: https://source.denx.de/u-boot/u-boot.git
    mirror: source.denx.de.u-boot.u-boot.git
    revision: 127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3
    backend: uboot-kbuild
    defconfig: sifive_unmatched_defconfig
    depends:
      - opensbi:fw_dynamic.bin

  linux:
    repo: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
    mirror: git.kernel.org.pub.scm.linux.kernel.git.stable.linux.git
    revision: a607c8f744340ad2c2486d46e96b66df47caffba
    backend: linux-kbuild
    defconfig: sifiveinc-2026.01/meta-sifive/recipes-kernel/linux/linux-sifive-6.18.3+git/unmatched/defconfig
```

Manifests should be declarative. Build policy should not be hidden inside shell
fragments.

## 5.3 `cross/`

Meson cross files live at the repository root so all Meson projects can share
them.

Responsibilities:

```text
select compiler and binutils
describe host machine
provide cross_compile property
provide optional toolchain/native helper paths
```

This follows the user's direction: toolchain configuration belongs in Meson's
cross-file mechanism.

The Unmatched build uses one pinned SDK cross file:

```text
cross/sifive-freedom-u-sdk.ini
```

`meson-unmatched/` may keep thin compatibility copies or refer to `../cross/`.

## 5.4 `schemas/`

Framework-owned schemas live here.

Responsibilities:

```text
validate board YAML
validate manifest YAML
validate boot-flow YAML
validate artifact placement YAML
keep validation independent of implementation scripts
```

The schema files should use JSON Schema against YAML parsed into a normal
JSON-compatible object. This keeps validation tooling broad and avoids inventing
a custom schema language.

Proposed files:

```text
schemas/board.schema.json
schemas/manifest.schema.json
schemas/boot-flow.schema.json
schemas/artifacts.schema.json
```

`3rdparty/schemas/` is reserved for externally imported schema files. Native
framework schemas belong in top-level `schemas/`.

## 5.5 `3rdparty/`

Small external dependencies that are vendored into this repository live here.
This directory exists to make external code ownership explicit, not to become a
dumping ground for upstream projects.

Allowed:

```text
small schema files
small helper scripts imported from another project
tiny single-purpose host tools
license files for vendored snippets
README files explaining provenance
```

Not allowed:

```text
OpenSBI source tree
U-Boot source tree
Linux source tree
Freedom-U-SDK source tree
toolchain binaries
download caches
generated build output
```

Large upstream projects should be represented by `manifests/` and checked out
under ignored source directories such as `meson-unmatched/src/` or
`downloads/`. This keeps git reviewable and avoids turning the study repo into
a source mirror.

Each vendored item should include:

```text
source URL
version or commit
license
reason for vendoring
local modifications, if any
update procedure
```

Suggested layout:

```text
3rdparty/
  README.md
  schemas/
    jsonschema/
      README.md
  tools/
    tiny-helper/
      README.md
      LICENSE
```

If an external dependency has its own build system, frequent upstream churn, or
more than a few source files, prefer manifest-managed checkout instead of
vendoring it in `3rdparty/`.

## 5.6 `patches/`

Patch series live here when the framework stops referencing patches directly
from `sifiveinc-2026.01/`.

Responsibilities:

```text
keep patch ownership explicit
organize by component and version
make experiments reviewable
avoid editing generated src/ trees as the source of truth
```

Proposed pattern:

```text
patches/u-boot/2026.01/0001-riscv-dts-add-fu740-pmu-events.patch
patches/linux/6.18/0001-unmatched-keep-led-settings.patch
patches/linux/6.18/pcie-debug/0001-fu740-print-link-state.patch
```

Patch series ordering should come from manifest files, not directory glob order.

## 5.7 `scripts/`

Top-level reusable framework scripts live here.

Responsibilities:

```text
read manifests
fetch sources
build components
deploy artifacts
verify artifacts
compare against Yocto deploy output
update boot partition only when explicitly requested
```

Scripts should be library-like and composable. `lab/` scripts can remain
operator convenience wrappers.

Proposed script split:

```text
repo-info.py
  print board, manifest, cross file, deploy paths

fetch-sources.py
  clone/reset/apply patches from manifest

build-component.py
  call backend by component name

deploy-artifacts.py
  copy and record artifact metadata

verify-artifacts.py
  check file type, DTB, FIT image, sizes

compare-yocto.py
  compare lite artifacts against known-good Yocto deploy

update-boot-partition.sh
  explicitly writes boot partition; never run implicitly
```

## 5.8 `meson-unmatched/`

This is the first concrete framework application.

Responsibilities:

```text
provide Meson targets for the Unmatched boot chain
bridge Meson cross properties into Make/Kbuild
produce deploy/ boot artifacts
serve as the reference implementation of the framework contracts
```

As the framework matures, duplicated facts should move from
`meson-unmatched/scripts/litebuild.py` into `manifests/` and `boards/`.

## 5.9 `docs/`

Documentation should be split by purpose:

```text
repo-framework.md       repository architecture
framework-spec.md       implementable framework contracts
pcie-study.md           technical PCIe notes
bringup/                board bring-up operations
pcie/                   focused PCIe workflows
decisions/              architecture decision records
```

Decision records keep design changes auditable. Example:

```text
docs/decisions/0003-use-meson-cross-files.md
```

UML source files:

```text
docs/uml/repository-components.puml
docs/uml/build-sequence.puml
docs/uml/data-model.puml
docs/uml/artifact-flow.puml
docs/uml/component-state.puml
docs/uml/command-surface.puml
docs/uml/dependency-graph.puml
docs/uml/sd-update-sequence.puml
```

Suggested ADR format:

```text
# 0003 Use Meson Cross Files for Toolchains

## Status
Accepted

## Context
...

## Decision
...

## Consequences
...
```

## 5.10 `tests/`

Tests should validate framework logic without requiring a board.

Responsibilities:

```text
manifest schema validation
script unit tests
artifact verifier tests using tiny fixtures
dry-run build command generation tests
```

Initial tests can be plain Python plus shell smoke tests. Do not introduce a
heavy test framework until it removes real friction.

## 6. Generated State Policy

Tracked:

```text
framework code
configuration
manifests
schemas
patches
documentation
small test fixtures
```

Ignored:

```text
sifiveinc-2026.01/
meson-unmatched/builddir/
meson-unmatched/src/
meson-unmatched/out/
meson-unmatched/deploy/
downloads/
toolchains/
*.wic
*.wic.xz
*.ext4
```

The repo should never accidentally commit a full SDK checkout or generated
image.

## 7. Workflow Model

## 7.1 First-Time Setup

```bash
./build.sh toolchain /path/to/freedom-u-sdk-toolchain.sh
meson setup meson-unmatched/builddir meson-unmatched \
  --cross-file cross/sifive-freedom-u-sdk.ini
```

## 7.2 Build Loop

```bash
ninja -C meson-unmatched/builddir check
ninja -C meson-unmatched/builddir opensbi-fw
ninja -C meson-unmatched/builddir u-boot
ninja -C meson-unmatched/builddir linux
```

## 7.3 PCIe Debug Loop

```text
edit patch or source
build component
deploy artifact
boot board
collect serial log
record observation in docs/pcie/
promote useful source edits into patches/
```

## 7.4 Known-Good Image Loop

```text
flash Yocto image once
replace boot artifacts from meson-unmatched/deploy/
keep rootfs stable
compare boot behavior against Yocto artifacts
```

## 8. Framework Contracts

The framework should treat every major step as a contract boundary. Meson
targets call scripts, scripts read manifests, manifests describe board and
component facts, and artifacts are verified before they are considered usable.

## 8.1 Source Contract

Every component must declare:

```text
repo URL
local mirror name
revision
patch series
source directory
dirty-source policy
```

## 8.2 Build Contract

Every component must declare:

```text
backend type
configure command
build command
required host tools
required dependency artifacts
```

## 8.3 Artifact Contract

Every component must declare:

```text
source artifact path
deploy artifact name
required or optional
verification method
consumer component
```

## 8.4 Verification Contract

Verifier types:

```text
exists
nonempty
elf-riscv
fit-image
dtb-readable
gzip-kernel
text-contains
```

Verification should run after every deploy and in a standalone target:

```bash
ninja -C meson-unmatched/builddir verify-artifacts
```

## 9. Command Surface

The repo should expose a small, predictable command surface. Users should not
need to remember the internal layout of OpenSBI, U-Boot, or Linux for normal
iterations.

Primary commands:

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

The framework command model should map to these internal phases:

```text
resolve      read board, manifest, cross-file properties
fetch        clone or reuse source mirrors
patch        apply ordered component patch series
configure    run defconfig or component configure step
build        run component backend
deploy       copy artifacts into deploy/
verify       validate artifact format and metadata
record       write deploy/manifest.json and logs
```

`fetch`, `patch`, `deploy`, and boot-media writes must be explicit operations.
Normal build targets may compile but should not silently modify an SD card.

The first stable user-facing wrapper can be:

```bash
./scripts/unmatched build opensbi
./scripts/unmatched build u-boot
./scripts/unmatched build linux
./scripts/unmatched deploy
./scripts/unmatched verify
./scripts/unmatched sd-update --device /dev/sdX --boot-only
```

The wrapper should call Meson/Ninja and framework scripts. It should not become
a second independent build system.

## 10. Data Model

The framework should be data-driven. Scripts can evolve, but board facts and
component versions should be readable without executing code.

Core data files:

```text
boards/hifive-unmatched-a00/board.yml
boards/hifive-unmatched-a00/boot-flow.yml
boards/hifive-unmatched-a00/artifacts.yml
manifests/unmatched-lite.yml
cross/sifive-freedom-u-sdk.ini
```

Configuration format policy:

```text
YAML       hand-written board and manifest data
Meson INI  toolchain cross files
JSON       generated machine-readable deploy metadata
PlantUML   architecture, sequence, and data-model diagrams
Markdown   explanations, checklists, decisions
```

YAML is the native framework format for hand-written board and manifest data.
It keeps nested component, artifact, and boot-flow data compact and matches the
style used by many embedded infrastructure repositories. Framework scripts
should parse it through a structured YAML library and validate the resulting
schema instead of relying on ad hoc string handling.

`board.yml` owns physical board facts:

```yaml
board: hifive-unmatched-a00
vendor: sifive
soc: fu740
arch: riscv64
console:
  baud: 115200
  linux: ttySIF0
boot_media:
  type: sd
  partition_table: gpt
```

`boot-flow.yml` owns stage order:

```yaml
stages:
  - name: opensbi
    provides:
      - fw_dynamic.bin
  - name: u-boot-spl
    consumes:
      - fw_dynamic.bin
    provides:
      - u-boot-spl.bin
  - name: u-boot-fit
    provides:
      - u-boot.itb
  - name: linux
    consumes:
      - hifive-unmatched-a00.dtb
    provides:
      - Image.gz
```

`artifacts.yml` owns boot-media placement:

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

`deploy/manifest.json` should be generated after every deploy:

```json
{
  "board": "hifive-unmatched-a00",
  "manifest": "unmatched-lite.yml",
  "cross_file": "sifive-freedom-u-sdk.ini",
  "components": {
    "opensbi": {
      "revision": "74434f255873d74e56cc50aa762d1caf24c099f8",
      "artifacts": ["fw_dynamic.bin"]
    }
  }
}
```

This gives every SD-card experiment a reproducible provenance trail.

## 11. Layering Model

The repo should stay split into four layers:

```text
reference layer      sifiveinc-2026.01/ known-good SDK and image artifacts
description layer    boards/ manifests/ schemas/ cross/ patches/
execution layer      meson-unmatched/ scripts/
evidence layer       deploy/ logs/ docs/ tests/
```

Rules:

```text
reference layer is ignored by git
description layer is the source of truth
execution layer implements contracts from description layer
evidence layer is mostly generated, except curated docs and small fixtures
```

This keeps the framework usable for learning: one can inspect what is being
built, why it is being built, which patches are applied, and which artifact was
written to the SD card.

## 12. Verification and CI Strategy

Most checks should run without an Unmatched board:

```text
YAML schema validation
manifest revision format validation
Meson setup smoke test
dry-run command generation
artifact verifier fixtures
patch series ordering checks
deploy manifest generation checks
```

Board-dependent checks should be separated:

```text
serial boot log capture
U-Boot PCIe enumeration
Linux lspci enumeration
NVMe or endpoint probe
PCIe error counter checks
```

The initial CI target can be local-only:

```bash
ninja -C builddir check
python3 -m pytest tests
```

Do not require network or board access for the default check target.

## 13. Naming Conventions

Components:

```text
opensbi
u-boot
linux
```

Boards:

```text
hifive-unmatched-a00
```

Artifacts:

```text
fw_dynamic.bin
u-boot-spl.bin
u-boot.itb
Image.gz
hifive-unmatched-a00.dtb
```

Manifests:

```text
<board>-<source-set>.yml
unmatched-lite.yml
freedom-u-sdk-2026.01.yml
```

Decision records:

```text
docs/decisions/NNNN-short-title.md
```

## 14. Migration Plan

## Phase 1: Documented Framework

Deliver:

```text
docs/repo-framework.md
meson-unmatched/DESIGN.md
cross files under meson-unmatched/cross/
basic Meson targets
```

## Phase 2: Promote Shared Configuration

Move or add:

```text
cross/
boards/hifive-unmatched-a00/
manifests/unmatched-lite.yml
schemas/
patches/
```

Refactor `litebuild.py` to read manifests instead of hardcoding component
facts.

## Phase 3: Add Verification and Metadata

Add:

```text
scripts/verify-artifacts.py
scripts/compare-yocto.py
deploy/manifest.json generation
tests/artifacts/
```

## Phase 4: Add Board Workflow Tools

Add:

```text
scripts/update-boot-partition.sh
docs/bringup/
docs/pcie/
docs/decisions/
```

Only after artifact generation and verification are solid.

## 15. Review Checklist

Before adding a new directory:

```text
Does it own source truth, generated output, docs, or tests?
Can it be regenerated?
Does it belong to board policy, component policy, or tooling?
Would Zephyr/Buildroot/U-Boot style separation make this clearer?
```

Before adding a new script:

```text
Does a manifest describe its inputs?
Can it run in dry-run mode?
Does it mutate source or media?
Does it print enough context to debug failure?
Is it board-specific or reusable?
```

Before adding a patch:

```text
Which component and version owns it?
Is the patch ordered in a manifest?
Is it temporary debug or intended behavior?
Can it be dropped when upstream moves?
```
