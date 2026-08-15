# Standalone U-Boot lwIP port

This directory contains the project-owned part of the educational lwIP port.
The official lwIP source is pinned and copied into a generated U-Boot tree by
`scripts/litebuild.py`; it is intentionally not vendored here.

Build with:

```bash
./build.sh u-boot-lwip-port
```

Read [`PORTING.md`](PORTING.md) when changing the adapter itself. It defines the
`netif`/DM_ETH contract, pbuf ownership, NO_SYS polling rules, protocol-module
lifecycle, and the checklist for adding a new protocol. For the complete
build, version-pinning, and board-validation tutorial, read
[`docs/network/lwip-port.md`](../../docs/network/lwip-port.md).
