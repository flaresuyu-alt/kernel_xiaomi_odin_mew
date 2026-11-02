/*
 * ksu_probe - freestanding aarch64 probe for the KernelSU / SukiSU driver interface.
 *
 * Reproduces exactly what a manager app does:
 *   1. reboot(KSU_INSTALL_MAGIC1, KSU_INSTALL_MAGIC2, 0, &fd)  -> driver fd
 *   2. ioctl(fd, KSU_IOCTL_GET_INFO, &info)                   -> version / flags
 *
 * Built with -nostdlib: raw syscalls only, no libc, no sysroot.
 * Caller is adb shell (uid 2000), which lacks CAP_SYS_BOOT, so if the KSU hook
 * is NOT active reboot() returns -EPERM and nothing reboots. Safe to run.
 */
#define NR_ioctl      29
#define NR_write      64
#define NR_exit_group 94
#define NR_reboot     142
#define NR_getuid     174
#define NR_geteuid    175

#define KSU_MAGIC1 0xDEADBEEFu
#define KSU_MAGIC2 0xCAFEBABEu

#define IOC(dir, type, nr, size) \
    (((unsigned long)(dir) << 30) | ((unsigned long)(size) << 16) | \
     ((unsigned long)(type) << 8) | ((unsigned long)(nr)))
#define IOR(type, nr, size) IOC(2, type, nr, size)   /* _IOC_READ = 2 on arm64 */

typedef unsigned long u64;
typedef long s64;
typedef unsigned int u32;

static s64 syscall4(u64 n, u64 a, u64 b, u64 c, u64 d)
{
    register u64 x8 __asm__("x8") = n;
    register u64 x0 __asm__("x0") = a;
    register u64 x1 __asm__("x1") = b;
    register u64 x2 __asm__("x2") = c;
    register u64 x3 __asm__("x3") = d;
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
                     : "memory", "cc");
    return (s64)x0;
}

static void out(const char *s, u64 n)
{
    syscall4(NR_write, 1, (u64)s, n, 0);
}

static void outs(const char *s)
{
    u64 n = 0;
    while (s[n])
        n++;
    out(s, n);
}

static void outdec(s64 v)
{
    char b[24];
    int i = 22;
    u64 u;
    int neg = v < 0;
    if (neg)
        u = (u64)(-v);
    else
        u = (u64)v;
    b[23] = '\n';
    if (u == 0)
        b[i--] = '0';
    while (u) {
        b[i--] = '0' + (u % 10);
        u /= 10;
    }
    if (neg)
        b[i--] = '-';
    out(&b[i + 1], (u64)(23 - i));
}

static void outhex(u64 v)
{
    char b[20];
    int i;
    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 16; i++) {
        int d = (int)((v >> ((15 - i) * 4)) & 0xf);
        b[2 + i] = d < 10 ? '0' + d : 'a' + d - 10;
    }
    b[18] = '\n';
    out(b, 19);
}

static char buf[256];

void _start(void)
{
    s64 r;
    int fd = -1;
    u32 cand[16];
    int ncand = 0;
    int i;

    outs("== ksu_probe ==\n");

    outs("uid=");
    outdec(syscall4(NR_getuid, 0, 0, 0, 0));
    outs("euid=");
    outdec(syscall4(NR_geteuid, 0, 0, 0, 0));

    outs("reboot(0xDEADBEEF,0xCAFEBABE,0,&fd) -> ret=");
    r = syscall4(NR_reboot, (u64)KSU_MAGIC1, (u64)KSU_MAGIC2, 0, (u64)&fd);
    outdec(r);
    outs("fd=");
    outdec(fd);
    outs("(ret=-1 => hook inactive, errno in -ret; ret=0 => KSU handled it)\n");

    if (fd < 0) {
        outs("no driver fd; nothing else to probe\n");
        syscall4(NR_exit_group, 0, 0, 0, 0);
        for (;;)
            ;
    }

    /* KSU_IOCTL_GET_INFO and its _LEGACY twin, tried across every plausible
     * struct size so an unknown struct layout cannot hide the answer. */
    cand[ncand++] = (u32)IOR('K', 2, 4);
    cand[ncand++] = (u32)IOR('K', 2, 8);
    cand[ncand++] = (u32)IOR('K', 2, 12);
    cand[ncand++] = (u32)IOR('K', 2, 16);
    cand[ncand++] = (u32)IOR('K', 2, 20);
    cand[ncand++] = (u32)IOR('K', 2, 24);
    cand[ncand++] = (u32)IOR('K', 2, 32);
    cand[ncand++] = (u32)IOR('K', 2, 40);
    cand[ncand++] = (u32)IOC(2, 'K', 2, 0);

    for (i = 0; i < ncand; i++) {
        int k;
        for (k = 0; k < 64; k++)
            buf[k] = 0;
        r = syscall4(NR_ioctl, (u64)fd, (u64)cand[i], (u64)buf, 0);
        outs("  ioctl req=");
        outhex(cand[i]);
        outs("      ret=");
        outdec(r);
        if (r == 0) {
            u32 *w = (u32 *)buf;
            outs("      GET_INFO ok: [0]=");
            outhex(w[0]);
            outs("                   [1]=");
            outhex(w[1]);
            outs("                   [2]=");
            outhex(w[2]);
            outs("                   [3]=");
            outhex(w[3]);
            break;
        }
    }

    /* KSU_IOCTL_GET_FULL_VERSION = _IOC(_IOC_READ, 'K', 100, 0); perm always_allow.
     * The driver fills a 255-byte full version string, e.g.
     * v4.3.0-<shortsha>@<branch>. This is what the manager compares against. */
    {
        unsigned long req = IOC(2, 'K', 100, 0);
        int k, len = 0;
        for (k = 0; k < 256; k++)
            buf[k] = 0;
        r = syscall4(NR_ioctl, (u64)fd, req, (u64)buf, 0);
        outs("GET_FULL_VERSION req=");
        outhex(req);
        outs("                 ret=");
        outdec(r);
        if (r == 0) {
            while (len < 200 && buf[len])
                len++;
            outs("                 string(len=");
            outdec(len);
            outs("): ");
            out(buf, (u64)len);
            outs("\n");
        }
    }

    /* KSU_IOCTL_HOOK_TYPE = _IOC(_IOC_READ, 'K', 101, 0); perm manager_or_root,
     * so uid 2000 is expected to be refused. */
    {
        unsigned long req = IOC(2, 'K', 101, 0);
        r = syscall4(NR_ioctl, (u64)fd, req, (u64)buf, 0);
        outs("GET_HOOK_TYPE req=");
        outhex(req);
        outs("             ret=");
        outdec(r);
        outs("             (0 => this uid is manager or root; -1/-13 => refused)\n");
        if (r == 0) {
            int k, len = 0;
            while (len < 64 && buf[len])
                len++;
            outs("             hook_type: ");
            out(buf, (u64)len);
            outs("\n");
        }
    }

    syscall4(NR_exit_group, 0, 0, 0, 0);
    for (;;)
        ;
}
