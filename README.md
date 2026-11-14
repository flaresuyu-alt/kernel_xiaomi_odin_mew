# kernel_xiaomi_odin_mew

小米 MIX 4（`odin` / SM8350）的 LineageOS 24.0 内核，在 [Inkypen79/kernel_xiaomi_odin](https://github.com/Inkypen79/kernel_xiaomi_odin) 基线之上集成
SukiSU-Ultra、SUSFS、ReKernel-X、ipset 与 NoMount。

> **纯个人自用内核**，不提供任何形式的担保。刷机风险自负。

## 基本信息

| 项 | 值 |
|---|---|
| 设备 | Xiaomi MIX 4（`odin`、`2106118C`） |
| 平台 | Qualcomm SM8350 (lahaina) |
| 内核版本 | `5.4.302-mew-extra` |
| 系统基线 | LineageOS 24.0（Android 15） |
| 内核基线 | CAF `msm-5.4`，内核 5.4.302 |
| 构建 | GitHub Actions，`odin_defconfig`，clang r416183b |

## 上游与致谢

本仓库是以下项目的派生作品，原始版权归各作者所有。

### 内核基线

| 项目 | 作者 | 仓库 |
|---|---|---|
| kernel_xiaomi_odin | Nicholas Magill（Inkypen79） | [Inkypen79/kernel_xiaomi_odin](https://github.com/Inkypen79/kernel_xiaomi_odin) |

本仓库内核代码由此派生，保留了上游完整提交历史（含上游 tip `550c3503d740`）。
基线本身以上游 CAF `msm-5.4` 为基础，再叠加 LineageOS 24.0 与 odin 设备适配。

### 集成的第三方组件

| 组件 | 作者 / 维护者 | 仓库 | 许可证 |
|---|---|---|---|
| SukiSU-Ultra | SukiSU-Ultra 团队 | [SukiSU-Ultra/SukiSU-Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra) | GPL-2.0 |
| SUSFS | simonpunk | [simonpunk/susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) | GPL-2.0 |
| ReKernel-X | myflavor（基于 nep_timeline 的 Re-Kernel） | [myflavor/ReKernel-X](https://github.com/myflavor/ReKernel-X) | GPL-2.0 |
| NoMount | maxsteeel | [maxsteeel/nomount](https://github.com/maxsteeel/nomount) | GPL-2.0-only |
| ipset | Jozsef Kadlecsik 等 | [ipset.netfilter.org](https://ipset.netfilter.org/) | GPL-2.0-only |

Linux 内核本体为 `GPL-2.0 WITH Linux-syscall-note`，见仓库根 [COPYING](COPYING)。
所有第三方组件按其原始许可证分发，未做任何许可证变更。

## 集成组件说明

| 组件 | 版本 | 作用 |
|---|---|---|
| SukiSU-Ultra | 子模块（4680 提交） | 内核级 root，KernelSU 分支，含 KPM |
| SUSFS | v2.3.0 | 路径、挂载、kstat、maps 等隐藏 |
| ReKernel-X | v2.0-odin | 冻结进程的 Binder / 信号 / 网络事件上报 |
| ipset | 5.4 上游 | 内核态 IP 集合，全部 set 类型内建 |
| NoMount | 协议 v22 | VFS 层路径重定向，无需改动挂载表 |

以上均为**内建**（`=y`）而非可加载模块：AnyKernel3 只分发 `Image.gz`，模块无法随包送达设备。
CI 中断言校验了配置与符号，若配置回退为 `=m` 或驱动被裁掉，构建会直接失败。

## 构建

编译在 GitHub Actions 完成，不在本地进行：

```bash
gh workflow run build-kernel.yml --ref lineage-24.0
gh run list -R flaresuyu-alt/kernel_xiaomi_odin_mew --limit 3
```

产物保留 30 天：

- `odin-kernel-<sha>` — `Image.gz`、设备 dtb、`System.map`
- `odin-anykernel3-<sha>` — AnyKernel3 刷机包 `odin-ksu-<sha>.zip`

## 刷入

```bash
# 在 Recovery 或内核管理器中直接刷入
odin-ksu-<sha>.zip

# 或解包后经 fastboot 刷 boot 分区
fastboot flash boot boot.img
```

刷入前请备份原 `boot` 分区。若首次启动停在第一屏，可用 `fastboot` 回刷原镜像恢复。

## 目录说明

| 路径 | 内容 |
|---|---|
| `drivers/rekernel_x/` | ReKernel-X 内建驱动 |
| `fs/nomount/` | NoMount 子系统（上游 v2.2.0 原样引入） |
| `fs/susfs.c`、`include/linux/susfs*` | SUSFS |
| `drivers/kernelsu` → `KernelSU/kernel` | SukiSU 子模块符号链接 |
| `patches/sukisu-ultra-5.4/` | SukiSU 在 5.10 以下内核的适配补丁 |
| `docs/android-common-kernel-patching.md` | 上游 CAF 的补丁提交规范（存档） |

## 备注

- `CONFIG_LOCALVERSION` 为 `-mew-extra`；`Makefile` 显式设置空的 `LOCALVERSION`，避免 setlocalversion 在非 tagged 树上追加 `+`。
- SUSFS 在 `fs/` 下的源码未附 SPDX 头，为上游一贯风格；许可证以 [simonpunk/susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) 的 `LICENSE` 为准。
- 本仓库仅作个人使用与留档，不接受功能请求，不保证跟进上游更新。
