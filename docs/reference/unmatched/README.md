# HiFive Unmatched Reference Archive

Downloaded: 2026-07-08

This directory mirrors the public HiFive Unmatched reference files linked from
SiFive's board page and document endpoints. It is intended as an offline
reference cache for board bring-up, boot-chain work, PCIe study, and QEMU/rootfs
comparison.

## Source Pages

- Board page: https://www.sifive.com/boards/hifive-unmatched
- Product brief: https://www.sifive.com/document-file/hifive-unmatched-product-brief
- Datasheet: https://www.sifive.com/document-file/hifive-unmatched-datasheet
- Software reference manual: https://www.sifive.com/document-file/hifive-unmatched-software-reference-manual
- Freedom U740-C000 manual: https://www.sifive.com/document-file/freedom-u740-c000-manual
- Schematics: https://www.sifive.com/document-file/hifive-unmatched-schematics
- Freedom-U-SDK releases: https://github.com/sifive/freedom-u-sdk/releases/latest

## Files

### Hardware

- `hardware/hifive-unmatched-product-brief.pdf`
- `hardware/hifive-unmatched-datasheet.pdf`
- `hardware/freedom-u740-c000-manual-v1p7.pdf`
- `hardware/hifive-unmatched-schematics-v3.pdf`
- `hardware/bill-of-materials-hifive-unmatched-3b0.pdf`

### Software

- `software/hifive-unmatched-sw-reference-manual-v1p1.pdf`

### Getting Started

- `getting-started/hifive-unmatched-getting-started-guide-v1p4_EN.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_FR.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_ES.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_IT.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_DE.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_PL.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_NL.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_SV.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_JP.pdf`
- `getting-started/hifive-unmatched-getting-started-guide-v1p4_ZH.pdf`

### Mechanical

- `mechanical/hf105-mech-3.step`

## Integrity

Checksums are in `CHECKSUMS.sha256`.

Verify with:

```bash
cd docs/reference/unmatched
sha256sum -c CHECKSUMS.sha256
```
