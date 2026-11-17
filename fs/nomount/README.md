# NoMount

Vendored from https://github.com/maxsteeel/nomount, tag v2.3.0-720
(commit ab93ee32a589ccb021257b3c1f280e058520d844), path `kernel/src/`.

License: GPL-2.0-only (kernel part).

The four files here (`nomount.c`, `nomount.h`, `Kconfig`, `Makefile`) are kept
byte-identical to upstream so that a future version bump is a plain copy.

Upstream integration notes:
https://github.com/maxsteeel/nomount/blob/v2.3.0-720/kernel/README.md

Wire protocol version is `23` (see `NOMOUNT_BASE_VERSION` in `nomount.h`).
The NoMount module ZIP requires a driver of at least v22 to install.