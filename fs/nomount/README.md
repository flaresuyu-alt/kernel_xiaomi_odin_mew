# NoMount

Vendored from https://github.com/maxsteeel/nomount, tag v2.2.0
(commit bf1847f98fed382369c43bfd652cd93774b9b8e3), path `kernel/src/`.

License: GPL-2.0-only (kernel part).

The four files here (`nomount.c`, `nomount.h`, `Kconfig`, `Makefile`) are kept
byte-identical to upstream so that a future version bump is a plain copy.

Upstream integration notes:
https://github.com/maxsteeel/nomount/blob/v2.2.0/kernel/README.md

Wire protocol version is `22` (see `NOMOUNT_BASE_VERSION` in `nomount.h`).
The NoMount module ZIP requires a driver of at least v22 to install.