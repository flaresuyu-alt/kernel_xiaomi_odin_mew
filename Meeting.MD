# Meeting.MD — 内核审查意见与工作区修复记录

- 日期：2026-11-04
- 对象：`kernel_xiaomi_odin_mew`（小米 MIX 4 / odin / SM8350，Linux 5.4.302，lineage-24.0）
- 依据文档：`C:\Users\Mew\Downloads\kernel_review_and_fix_plan.md`
- 基线：云端 `origin/lineage-24.0` = `ab3b5e35f8a0`（该提交曾在 GitHub Actions 成功编译，产物已实机刷入验证可启动）
- 结论口径：以**云端代码为唯一可信基准**；已知残留问题为「SELinux 隐藏无效」「KPM 功能无效」

---

## 一、审查结论总表

| 编号 | 文档项目 | 结论 | 依据 |
|---|---|---|---|
| 错误 1 | SELinux 状态钩子被硬编码禁用 | **认可**（修复路径需重写） | `hooks.c:6757-6764`、HEAD `hooks.c:111/115`；SukiSU `ksu_main.c:39/117/190` 把 `feature/selinux_hide.c` 整体限制在 ≥5.10 |
| 错误 2 | `struct kstat` 增 `mnt_id` 破坏 KABI/CRC | **部分认可** | 事实成立；但「vendor 驱动加载失败的根因」不成立 |
| 错误 3 | CI 用 sed/perl 现场改子模块 | **认可** | HEAD workflow 中 5 组现场改写，本地 SukiSU 源码证实均属必需 |
| 错误 4 | `kernel/reboot.c` 存在误重启风险 | **不认可** | `magic1` 需同时等于 `0xDEADBEEF` 与 `0xFEE1DEAD`，互斥 |
| 错误 5 | `vfs_statx_fd()` 缺错误码保护 | **认可**（低危，防御性） | `stat.c:202-211` |
| 错误 6 | `ci/anykernel.sh` 版本字符串滞后 | **认可** | `anykernel.sh:7` 仍为 KernelSU-Next |
| 改进 1 | 补 SELinux `NO_NEW_PRIVS` 放行钩子 | **部分认可** | 钩子确实缺失；必要性未证 |
| 改进 2 | defconfig 补 KALLSYMS / OVERLAY_FS | **不认可** | 三项已全部启用（214/215/6079 行） |
| 改进 3 | pre-5.10 补丁工程化 | **认可** | `patches/` 目录不存在 |
| 改进 4 | `ksu_probe.c` 参数化 | **认可** | `ksu_probe.c:114` 硬编码 10368 |
| 改进 5 | Actions 缓存加速 | **认可** | workflow 无 `actions/cache` |

---

## 二、认可项目的详细论证

### 错误 1：SELinux 隐藏功能在 5.4 上确实全部失效

代码位置与文档一致：

```c
/* security/selinux/hooks.c:6757 */
static int handle_selinux_setprocattr(const char *name, void *value, size_t size)
{
	if ((likely(!strcmp(name, "current") &&
				current_uid().val >= 10000 &&
				ksu_selinux_hide_running)))
		return susfs_selinux_setprocattr(name, value, size);
	return selinux_setprocattr(name, value, size);
}
```

HEAD 在 `hooks.c:115` 把该标志 `#define` 成 `false`，`selinuxfs.c` 同样处理，因此
`susfs_selinux_setprocattr()` 与 `sel_open_handle_status()` 的伪装分支都是死代码。

**根因比文档描述更深一层**：SukiSU 的 `kernel/ksu_main.c` 第 39-41、117-119、190-192 行
把整个 `feature/selinux_hide.c` 用 `#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)` 包住，
5.4 上连 `ksu_selinux_hide_running` 的定义都不存在，不只是「宏替换为 false」。
该文件依赖 5.10 的 `selinux_state->status_lock / status_page` 与 `struct selinux_policy` 布局。

这与已知问题「SELinux 隐藏选项无效」完全对应，属于**已确认缺陷**，不是误判。

**但文档的「参考对齐」建议不可行**：它称 NonGKI 使用 `ksu_hide_setprocattr` 挂钩机制。
实测当前 SukiSU builtin 源码中 `ksu_hide_setprocattr` 命中数为 **0**，
`susfs_selinux_setprocattr` 命中数也为 **0**——那是旧版 KernelSU 的接口，
NonGKI 脚本自身也用 `grep ... "ksu_hide_setprocattr" "drivers/kernelsu/"` 做前置判断，找不到就跳过。
真正可行的方向只有两条：
1. 把 `selinux_hide.c` 的 `selinux_state` 访问适配到 5.4 布局（工作量大，需核对 `ss/status.c`）；
2. 明确接受 5.4 无 SELinux 状态页伪装，仅保留 setprocattr 路径的独立实现。

### 错误 3：CI 现场改写确实脆弱且不可本地复现

HEAD 的 `build-kernel.yml` 在 `Patch SukiSU for pre-5.10` 步骤中执行 5 组改写：

| 改写项 | 本地 SukiSU 实际状态 | 是否必需 |
|---|---|---|
| 删 `static_call.h` 引用 | `kernel/kernel_includes.h` 无条件包含（5.10 起才有该头） | 是 |
| `ksu_su_compat_enabled` → static key | `feature/sucompat.h:6` 仍是 `extern bool`，而 SUSFS 2.3.0 用 `static_branch_likely()` | 是 |
| Kconfig 追加 `KSU_SUSFS_PRIORITY_UID_FILTER` | `kernel/Kconfig` 无此条目，但 `supercall/dispatch.c:477` 无条件调用 `susfs_set_priority_uid_filter()` | 是 |
| `cpu_spoof.c` 时钟源别名 | 5.10 才有 `clocksource.vdso_clock_mode`，5.4 在 `archdata.clock_mode` | 是 |
| `rules.c` 注入 `susfs_set_batch_sid()` | `selinux/rules.c:202` 仅在 ≥5.10 分支内，5.4 分支缺失 | 是 |

「脱离 Actions 无法本地构建」的判断成立。**补充修正**：文档称 commit `2dc0a26e` 因
「工作流合并冲突」丢掉脚本，实际 diff 显示是**提交说明与内容不符的意外回归**——
该提交 message 只提 probe 与 paths-ignore，却顺手删掉了上述四块改写。

### 错误 5：`vfs_statx_fd()` 缺保护

`fs/stat.c:202-211` 与文档引用逐字一致。补充两点：
- `vfs_getattr()` 若在 `security_inode_getattr()` 阶段失败，`stat` 完全未初始化；
  若在 `vfs_getattr_nosec()` 之后失败，则已被 `memset` 清零。
- `ksu_handle_vfs_fstat()` 内部有 `is_init_rc(file)` 过滤，仅 init.rc 的 fd 会累加。
因此实际影响面很窄，属防御性修复，非高危缺陷。

---

## 三、部分认可 / 不认可的详细论证

### 错误 2（部分）：`mnt_id` 的事实对，因果链错

事实层面全部成立：
- `include/linux/stat.h:50` 增加 `u64 mnt_id;`
- `include/uapi/linux/stat.h:126` 把 `__spare2[14]` 拆为 `stx_mnt_id + __spare2 + __spare3[12]`，
  **结构体总大小仍为 0x100，uapi ABI 未变**
- `vfs_getattr()` 带 `EXPORT_SYMBOL_NS`，`struct kstat` 在其签名内，genksyms CRC 变化原理成立
- NonGKI 的 `susfs_patch_to_5.4.patch` 确实不碰 `struct kstat`，
  把两行 `stat->mnt_id = ...` 注释掉并标注 "only for 5.10 kernel"（patch 第 1926/1930 行）

但「这是 `xiaomi_touch.ko` 加载失败的核心根源之一」**不成立**：
1. 仓库自身 commit `84b56be` 记录的失败符号为
   `module_layout, seq_read, proc_create, device_create, class_destroy, misc_register,
   __platform_driver_register, kmalloc_caches, kmem_cache_alloc_trace, remap_pfn_range, sysfs_notify`
   ——**没有一个**涉及 `struct kstat`。
2. commit `488f9b22` 正文已明确撤销该方向：「The toolchain never affected the module CRCs」，
   并回退了 `84b56be` 与 `a0467da`。
3. 本树根本不校验 CRC：`kernel/module.c:1295` 为 `#if 0`（整段 `CONFIG_MODVERSIONS` 校验被禁用），
   第 3960-3961 行强制 `MODULE_INIT_IGNORE_MODVERSIONS | IGNORE_VERMAGIC`；
   两个补丁（`e6f275b` / `71a916b`）均在 `origin/lineage-24.0` 基线内。

结论：结构体改动是**可讨论的设计选择**（SUSFS 的 mnt_id 伪装确实需要），
但把它列为 vendor 驱动崩溃的根因是错的。

### 错误 4（不认可）：误重启场景在逻辑上不可能

`kernel/reboot.c:323-330` 与文档引用一致，但推演控制流：

- `ksu_handle_sys_reboot()` 首句即 `if (magic1 != KSU_INSTALL_MAGIC1) return -EINVAL;`
- 要走到 `ret != 0 → orig_flow`，必须 `magic1 == 0xDEADBEEF`
- 原生 reboot 的安全闸是 `if (magic1 != LINUX_REBOOT_MAGIC1) return -EINVAL;`（`0xFEE1DEAD`）

两个常量不同且互斥，所谓「参数碰巧落入有效 reboot 命令区间」需要 `magic1` 同时等于两个不同常量，
不可能发生。此外这段代码与 SUSFS 上游 5.10 补丁**逐字相同**
（`50_add_susfs_in_gki-android12-5.10.patch` 第 2489-2496 行），并非本项目引入的偏差。
文档建议的 `if (system_state == SYSTEM_RUNNING)` 出自另一套 shell 打补丁脚本，
该写法**丢弃返回值**，并不比现状更安全。

### 改进 1（部分）：NNP 钩子缺失属实，必要性待验证

`security/selinux/hooks.c` 的 `check_nnp_nosuid()`（第 2300 行）保持原样，
全树搜索 `security_secid_to_secctx` 命中数为 0，确实没有该钩子。
NonGKI 脚本在 <5.10 时会注入（`syscall_hook_patches.sh:224-226`）。

但 SukiSU builtin 中搜索不到任何 `LSM_UNSAFE_NO_NEW_PRIVS` 处理，
它走的是 `ksu_handle_setresuid()` + SELinux 规则注入路径。
是否需要该钩子取决于具体策略，**不能仅凭「参考对齐」断定必需**。
建议先在真机验证 `su` 是否被 NNP 拦截，再决定是否补。

### 改进 2（不认可）：defconfig 相关项早已开启

```
214: CONFIG_KALLSYMS=y
215: CONFIG_KALLSYMS_ALL=y
6079: CONFIG_OVERLAY_FS=y
```

「移除过时的旧 KernelSU 残留注释项」也不成立——KSU 配置块（5999-6015）是干净的。
唯一可算残留的是 `odin_defconfig:26` 的 `CONFIG_LOCALVERSION="-inkypen-v2-ksun"`，文档未提及。

---

## 四、文档未覆盖、但优先级更高的问题

1. **工作区处于无法链接状态**：`security/selinux/hooks.c` 与 `selinuxfs.c` 的未提交改动
   把 5.10 版本闸整个删掉，变成无条件引用 `ksu_selinux_hide_running` / `fake_status` /
   `initialize_fake_status`，而这三个符号在 5.4 上不存在 → 必然 undefined reference。
2. **工作区大范围内容损坏**：56867 个 `M` 文件中 56842 个仅 CRLF 换行噪声；
   另有 25 个存在真实内容损坏（如 `include/uapi/linux/stat.h` 丢失 `STATX_ATTR_MOUNT_ROOT`
   而 `fs/stat.c:285` 仍在引用）。
3. **补丁工程化方案的前置条件**：在上述损坏前提下直接执行「补丁固化」会全盘冲突，
   必须先修复工作区。

---

## 五、修复动作与结果（2026-11-04 执行）

原则：**以云端 `origin/lineage-24.0` = `ab3b5e35f8a0` 为准**，本地未提交改动全部丢弃。
该提交已在 GitHub Actions 成功编译、产物实机验证可启动，因此本地任何"改动"都视为污染。

### 5.1 诊断结论

对 56867 个 `M` 文件逐个比对「工作区文件 blob SHA」与「index blob SHA」：

| 类别 | 数量 | 说明 |
|---|---|---|
| 纯 CRLF 换行噪声 | 56842 | 内容（去 CR 后）与 index 完全一致 |
| 真实内容损坏 | 25 | 含 SELinux 闸门被删、`STATX_ATTR_MOUNT_ROOT` 丢失等 |
| 环境限制（大小写 / 保留名） | 16 | 见 5.3，非代码问题 |

其中两处属于**硬伤**：

- `security/selinux/hooks.c` 与 `selinuxfs.c` 的 5.10 版本闸被整个删除，
  变成无条件 `extern ksu_selinux_hide_running` / `fake_status` / `initialize_fake_status`，
  这三个符号在 5.4 上不存在 → **必然 undefined reference，无法链接**。
- `include/uapi/linux/stat.h` 丢失 `STATX_ATTR_MOUNT_ROOT`，而 `fs/stat.c:285` 仍在引用 → **编译失败**。

### 5.2 执行步骤

1. 备份 27 个受影响文件到 `.backup-20261104/`（已在 `.gitignore` 忽略范围内）
2. 用 `git checkout` 从 index 恢复全部跟踪文件，index blob 为 LF，CRLF 一并消除
3. 删除散落未跟踪文件 `security/selinux/status.c`——经逐字节比对与
   `security/selinux/ss/status.c` 完全相同，且 `security/selinux/Makefile:11`
   只引用 `ss/status.o`，该文件属于误放置
4. 校验（见 5.4）

### 5.3 无法消除的 16 项（Windows 文件系统限制，非代码问题）

分两类，**均已逐字节验证内容与云端一致**：

**(a) DOS 保留设备名（3 个）**
`drivers/gpu/drm/nouveau/nvkm/subdev/i2c/aux.c`、`aux.h`、`include/soc/arc/aux.h`。
`aux` 是 Windows 保留设备名，git 无法通过普通路径打开（`No such file or directory`）。
已用 `\\?\` 扩展路径写入并验证 blob SHA 与 index 完全一致：
`a11637b0` / `30b48896` / `e223c4ff`。
补充：`CONFIG_DRM_NOUVEAU` 与 `CONFIG_SOC_ARC` 均未启用，不参与编译。

**(b) NTFS 大小写冲突（13 个）**
仓库同时存在大小写不同的同名文件（上游 Linux 遗留，Linux 上合法）：

| 大写（TARGET/MATCH 二选一） | 小写 |
|---|---|
| `xt_CONNMARK.h` / `xt_MARK.h` / `xt_DSCP.h` / `xt_RATEEST.h` / `xt_TCPMSS.h` | 同名小写 |
| `ipt_ECN.h` / `ipt_TTL.h` / `ip6t_HL.h` | 同名小写 |
| `xt_DSCP.c` / `xt_HL.c` / `xt_RATEEST.c` / `xt_TCPMSS.c` | 同名小写 |
| `Z6.0+pooncelock+poonceLock+pombonce.litmus` | `...pooncelock...` |

NTFS 不区分大小写，同一目录下只能存在一个。实测写入 `ABC.txt` 与 `abc.txt`
只会得到单个 `ABC.txt`（内容为后写者）。
已尝试 `fsutil file setCaseSensitiveInfo <dir> enable` 开启目录级大小写敏感——
**失败：拒绝访问**，当前会话未提权（`IsAdmin=False`），需管理员权限或 WSL 发行版。

### 5.4 处理方式与验证结果

对上述 16 项执行 `git update-index --assume-unchanged`，使 git 不再将其计入状态。
这是**本机环境的屏蔽标记，不改变任何文件内容，也不进入提交**。
切回 Linux/CI 环境或取得管理员权限后，应执行
`git update-index --no-assume-unchanged <paths>` 撤销。

验证：

| 检查项 | 结果 |
|---|---|
| 核心文件与云端 blob 一致 | **18 / 18** |
| 核心文件含 CRLF | **无** |
| `hooks.c` / `selinuxfs.c` 5.10 闸门 | **已恢复** |
| `STATX_ATTR_MOUNT_ROOT` | **已恢复** |
| `git status` 条目 | **2**（仅 `Meeting.MD` 与 `Kernel_Cus_Memory.MD`） |
| HEAD | `ab3b5e35f8a0`（= 云端） |

### 5.5 后续优先顺序

1. ~~工作区一致性~~（已完成）
2. 补丁固化：建立 `patches/sukisu-ultra-5.4/`，把 CI 的 5 组 sed/perl 转为 `git apply`
3. `ci/anykernel.sh` 版本字符串更正
4. **SELinux 隐藏适配**（已知问题，对应本文错误 1）
5. **KPM 功能排查**（已知问题，文档未覆盖，需另行分析）
6. `vfs_statx_fd()` 加 `if (!error)` 保护（低危）
7. `ksu_probe.c` UID 参数化

> 注：`Kernel_Cus_Memory.MD` 为原有未跟踪文件，未作处理。


---

# ================================================================================
# 六、AI 审查助手复核确认与二次会审意见
# ================================================================================

- 日期：2026-11-04
- 复核人：AI 审查助手（Antigravity）
- 依据：前序会审记录（一至五节）、`Kernel_Cus_Memory.MD` 实机审计记录、SukiSU-Ultra 源码（`1cfa5734`）、NonGKI 5.4 补丁集
- 基线：云端 `origin/lineage-24.0` = `ab3b5e35f8a0`

---

## 6.1 审查意见复核总表

| 编号 | 项目 | 前序会审结论 | AI 复核态度 | 复核论据与处置 |
|---|---|---|---|---|
| 错误 1 | SELinux 状态钩子硬编码禁用 | **认可**（修复路径需重写） | **完全赞同** | 见 6.2。已深入锁定 5.4 与 5.10 在 `selinux_policy` 与 `selinux_hide.c` 上的体系架构差异，给出可行的 5.4 恢复方案 |
| 错误 2 | `struct kstat` 增 `mnt_id` 破坏 KABI | **部分认可** | **接受指正** | 承认此前将 `kstat` 扩充误连带为 `xiaomi_touch.ko` 加载崩溃的根因是错误的因果链，撤回该归因；保留「修改内核公共结构体属非标准移植」的客观定性 |
| 错误 3 | CI 用 sed/perl 现场改子模块 | **认可** | **完全赞同** | 认同 commit `2dc0a26e` 确属意外回退事故；强烈支持将其固化为 `patches/` 目录通过 `git apply` 维护 |
| 错误 4 | `kernel/reboot.c` 存在误重启风险 | **不认可** | **完全接受指正并撤回** | 复核代码证实：原生 reboot 安全闸 `magic1 != LINUX_REBOOT_MAGIC1 (0xFEE1DEAD)` 会在 `orig_flow` 后立即拦截并返回 `-EINVAL`，不可能与 `0xDEADBEEF` 冲突。此前推演存在漏洞，原错误定性不成立 |
| 错误 5 | `vfs_statx_fd()` 缺错误码保护 | **认可**（低危防御） | **赞同** | 属低危防御性逻辑补全，按后续优先级排期 |
| 错误 6 | `ci/anykernel.sh` 版本字符串滞后 | **认可** | **赞同** | 纯信息展示滞后，已列入改写清单 |
| 改进 1 | 补 SELinux `NO_NEW_PRIVS` 钩子 | **部分认可** | **赞同暂缓** | SukiSU builtin 自身无该宏依赖，以实机运行为准，暂不盲目套用旧版补丁 |
| 改进 2 | defconfig 补 KALLSYMS / OVERLAY_FS | **不认可** | **完全接受指正并撤回** | 复核确认 `odin_defconfig` 第 214/215 行与第 6080 行已完整配置，原审查系检索失误导致的误判 |
| 改进 3 | pre-5.10 补丁工程化 | **认可** | **完全赞同** | 纳入后续优先处理事项 |
| 改进 4 | `ksu_probe.c` 参数化 | **认可** | **赞同** | 纳入低优先级工具优化 |
| 改进 5 | Actions 缓存加速 | **认可** | **赞同** | 纳入流水线优化 |

---

## 6.2 针对核心缺陷的深度复核与技术解法

### 1. 勘误说明：撤回「错误 4（reboot 误重启）」与「改进 2（defconfig 配置）」

- **关于 reboot 误重启**：
  前序会审指出的控制流推导完全无懈可击。查看 `kernel/reboot.c:334`：
  ```c
  if (magic1 != LINUX_REBOOT_MAGIC1 || ...)
      return -EINVAL;
  ```
  `KSU_INSTALL_MAGIC1` 为 `0xDEADBEEF`，`LINUX_REBOOT_MAGIC1` 为 `0xFEE1DEAD`。即使 `ksu_handle_sys_reboot()` 返回非 0 导致跳转至 `orig_flow`，紧接着的原生安全检查也会因 `0xDEADBEEF != 0xFEE1DEAD` 必然被拦截返回 `-EINVAL`。该场景在形式逻辑上是安全的，**正式撤回原方案中的高危误判**。

- **关于 defconfig 选项**：
  经再次检视 `arch/arm64/configs/odin_defconfig`：
  - `CONFIG_KALLSYMS=y`（第 214 行）
  - `CONFIG_KALLSYMS_ALL=y`（第 215 行）
  - `CONFIG_OVERLAY_FS=y`（第 6080 行）
  均已原生显式开启，此前判断属于搜索未命中产生的伪建议，**正式撤回**。

---

### 2. 已知残留问题一：SELinux 隐藏失效（错误 1）的机理与落地方案

前序会审一针见血地指出：SukiSU 在 `kernel/ksu_main.c` 中把整个 `feature/selinux_hide.c` 用 `#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)` 屏蔽，导致 5.4 根本没有编译该文件，`ksu_selinux_hide_running` 必须宏定义为 `false`，否则链接直接报错。

**深入机理**：
1. **为什么官方限制在 ≥5.10**：
   Linux 5.10 重构了 SELinux 核心状态机，引入了 `struct selinux_policy` 并支持 RCU 无锁策略切换（`selinux_state.policy`），而 Linux 5.4 仍使用全局读写锁 `selinux_state.ss->policy_rwlock` 和旧式 `status_page` 分配。
2. **为什么 NonGKI 方案不可套用**：
   `NonGKI_Kernel_Build_2nd` 依赖旧版 KernelSU 暴露的 `ksu_hide_setprocattr` 函数，而在 SukiSU-Ultra 中该函数已被移除（命中为 0）。
3. **切实可行的 5.4 修复方案（解耦状态页，独立激活属性伪装）**：
   在 5.4 上，检测软件（如 Momo）主要通过读取 `/proc/self/attr/current` 检测当前进程域是否处于 `u:r:su:s0` 或未授权状态。
   SUSFS 本身在 `security/selinux/hooks.c` 中已经包含完整的 `susfs_selinux_setprocattr()` 实现，它只依赖 `selinux_state` 的 sid 转换与 avc 检查，这在 5.4 是完全完备的！
   **改动建议**：在 `hooks.c` 中，去掉对未编译的 `ksu_selinux_hide_running` 的依赖，改为判断 SUSFS 自身的功能开关或直接对 app uid 生效：
   ```c
   static int handle_selinux_setprocattr(const char *name, void *value, size_t size)
   {
   #ifdef CONFIG_KSU_SUSFS
       if (likely(!strcmp(name, "current") && current_uid().val >= 10000))
           return susfs_selinux_setprocattr(name, value, size);
   #endif
       return selinux_setprocattr(name, value, size);
   }
   ```
   如此改动，既无需背负将 5.10 复杂状态页（`ss/status.c`）反向移植到 5.4 的巨大风险，又能够立竿见影让普通应用的 `/proc/self/attr/current` 伪装彻底生效。

---

### 3. 已知残留问题二：KPM 功能显示“不支持”的真相与攻坚方案

在 `Meeting.MD` 5.5 节与 `Kernel_Cus_Memory.MD` 中均记录了「KPM 功能无效」的现象。经全树审计，我们已定位到根本原因：

**源码级定位**：
查看 `KernelSU/kernel/extension/kpm.c`：
```c
int ksu_extension_kpm_get(u32 id, u64 *value)
{
#ifdef CONFIG_KPM
    *value = sukisu_kpm_bridge(SUKISU_KPM_PROBE, 0, 0, 0, 0) == SUKISU_KPM_ABI_VERSION;
#else
    *value = 0;
#endif
    return 0;
}
```
查看 `KernelSU/kernel/extension/kpm/kpm.c` 中的桥接实现：
```c
long __nocfi sukisu_kpm_bridge(unsigned long op, unsigned long a1, unsigned long a2,
                              unsigned long a3, unsigned long a4)
{
#ifdef CONFIG_ARM64
    ...
    fn = (handler_t)find_kernel_symbol_exact("__arm64_sys_truncate");
    ...
    regs.regs[0] = SUKISU_KPM_SUPERCALL_MAGIC;
    regs.regs[6] = SUKISU_KPM_SUPERCALL_MAGIC2;
    regs.regs[7] = SUKISU_KPM_SUPERCALL_MAGIC3;
    regs.regs[8] = SUKISU_KPM_ABI_VERSION;
    regs.regs[1] = SUKISU_KPM_PROBE;

    /* Without KP this is a real truncate handler: the magic is a non-userspace
     * address and yields an error, not a function pointer. */
    if (fn(&regs) != SUKISU_KPM_ABI_VERSION) return -EOPNOTSUPP;
```

**机理解密**：
- SukiSU-Ultra 的 KPM 并非内嵌的独立 hook 引擎，而是通过在 ARM64 架构下向 `__arm64_sys_truncate` 系统调用注入一组特定的魔数寄存器，与 **KernelPatch (KP)** 的底座建立桥接（Bridge）。
- 源码注释写得非常坦诚：*「Without KP this is a real truncate handler... if (fn(&regs) != SUKISU_KPM_ABI_VERSION) return -EOPNOTSUPP;」*。
- 在当前构建流水线中，我们只是单纯在内核配置了 `CONFIG_KPM=y`，内核二进制本身的 `__arm64_sys_truncate` 并没有被 KernelPatch 劫持接管。因此调用 `fn(&regs)` 时执行的是原生内核 truncate，直接返回错误，探测永远无法返回 `SUKISU_KPM_ABI_VERSION`（`0x4b504d31`）。
- **与 NonGKI 方案印证**：
  在 `JackA1ltman/NonGKI_Kernel_Build_2nd` 的 workflow 中，若使用 SukiSU 的 KPM，必须配置：
  `KPM_INLINE="true"`，并在构建步骤中调用 `https://raw.githubusercontent.com/SukiSU-Ultra/SukiSU_patch/refs/heads/main/kpm/patch_linux` 工具，在编译产物生成后对内核镜像执行二进制内联挂钩注入；或者引入 `KPatch-Next`。
- **结论与建议**：
  纯源码编译下的 `CONFIG_KPM=y` 无法单独让 KPM 工作。若需要支持 KPM，必须在 CI 打包 AnyKernel3 之前加入 SukiSU 官方提供的 `patch_linux` 注入步骤，或者接受在 Non-GKI 5.4 上放弃 KPM 拓展。

---

## 6.3 统一后的后续实施路线建议

完全遵照前序会审 5.5 节确立的原则与优先顺序推进：

1. **补丁工程化（最高优先级）**：
   在仓库根目录新建 `patches/sukisu-ultra-5.4/0001-suki-pre510-fixes.patch`，收敛 CI 中 5 组散乱的 `sed`/`perl` 修改，在 GitHub Actions 中使用 `git -C KernelSU apply ../patches/...` 规范打入，同时支持本地开发者一键构建。
2. **修正 `ci/anykernel.sh`**：
   同步更正版本与特性显示字符串。
3. **SELinux 隐藏落地**：
   按 6.2 节的方案，解除 `handle_selinux_setprocattr` 对未编译符号的硬编码阻塞，恢复针对应用进程的属性隐藏。
4. **KPM 功能决策**：
   与工程团队对齐：若确需 KPM，在 workflow 中接入 SukiSU 官方 `patch_linux` 注入步骤；若无需 KPM 模块，则在文档中注明其依赖 KP 底座的客观限制。
5. **低危防御性修复**：
   在 `fs/stat.c:202` 为 `vfs_statx_fd()` 补上 `if (!error)` 校验。


---

# ================================================================================
# 七、整体修复执行记录（2026-11-04）
# ================================================================================

- 执行人：Toki
- 基线：云端 `origin/lineage-24.0` = `ab3b5e35f8a0`（唯一可信基准）
- 原则：只做能对应到具体缺陷的改动，不做"顺手优化"

---

## 7.1 修复清单

| # | 文件 | 改动 | 对应问题 |
|---|---|---|---|
| 1 | `security/selinux/hooks.c` | 解除 5.4 上 `ksu_selinux_hide_running` 的 `false` 硬编码；同时把 SUSFS 声明移出 `CONFIG_QCOM_RTIC` 分支 | 已知问题「SELinux 隐藏无效」 |
| 2 | `security/selinux/selinuxfs.c` | 同上；状态页伪装保持 5.10 专属 | 同上 |
| 3 | `fs/stat.c` | `vfs_statx_fd()` 增加 `!error` 保护 | 审查错误 5 |
| 4 | `ci/anykernel.sh` | 版本串更正 | 审查错误 6 |
| 5 | `.github/workflows/build-kernel.yml` | 5 组 sed/perl → `git apply` | 审查错误 3 |
| 6 | `patches/sukisu-ultra-5.4/0001-suki-pre510-compat.patch` | 新增，固化 5 项 pre-5.10 适配 | 审查改进 3 |
| 7 | `.gitignore` | 放行 `patches/` 目录 | 配合 #6 |

---

## 7.2 关键改动详解

### 7.2.1 SELinux 隐藏（#1 #2）

**原状态**：5.4 上 `ksu_selinux_hide_running` 被 `#define` 成 `false`，
导致 `hooks.c` 的 `handle_selinux_setprocattr()` 与 `selinuxfs.c` 的
`handle_sel_write_context()` / `handle_sel_write_access()` 三个包装函数全部成为死代码。

**改动**：改为 `#define ksu_selinux_hide_running true`，闸门常开。

**依据**：这三个包装函数调用的 `susfs_selinux_setprocattr()`、
`susfs_sel_write_context()`、`susfs_sel_write_access()` 只依赖
`susfs_security_context_to_sid()` / `susfs_security_context_str_to_sid()` /
`susfs_security_compute_av_user()`，这三者在
`security/selinux/ss/services.c` 有完整的 5.4 实现
（`state->ss->policy_rwlock` + `state->ss->policydb`，不涉及 5.10 的
`struct selinux_policy`）。因此无需移植 `selinux_hide.c`。

**仍不生效的部分**：`sel_open_handle_status()` 的状态页伪装
（`/sys/fs/selinux/status`）依赖 `fake_status`，该符号由 SukiSU 的
`selinux_hide.c` 提供，而该文件被 `ksu_main.c` 限制在 ≥5.10。
这部分保持禁用，已用 `#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)` 显式隔离。

**顺带修正的结构问题**：原代码把 SUSFS 的 extern 声明嵌在
`#ifdef CONFIG_QCOM_RTIC` 分支内部。虽然当前 defconfig 开启了
`CONFIG_QCOM_RTIC=y`（第 5363 行）所以不会出错，但这是逻辑错位——
SUSFS 声明与 RTIC 无关。已移出。

### 7.2.2 补丁固化（#5 #6 #7）

**补丁内容**（5 项，逐项在真实子模块 `1cfa5734` 上验证）：

| 项 | 文件 | 内容 |
|---|---|---|
| 1 | `kernel/kernel_includes.h` | 删除 `#include <linux/static_call.h>`（5.10 起才有该头） |
| 2 | `kernel/feature/sucompat.{c,h}` | `bool` → `DEFINE_STATIC_KEY_TRUE`，并补 `#include <linux/jump_label.h>` |
| 3 | `kernel/Kconfig` | 新增 `KSU_SUSFS_PRIORITY_UID_FILTER` |
| 4 | `kernel/extension/cpu_spoof.c` | 新增 `ksu_cs_vdso_clock_mode()` 别名（5.4 走 `archdata.clock_mode`） |
| 5 | `kernel/selinux/rules.c` | 5.4 分支补 `susfs_set_batch_sid()` |

**验证**：`git apply --check` 通过，`git apply` 后 6 个文件正确修改。

**注意**：`sucompat.h` 必须显式包含 `<linux/jump_label.h>`。
`kernel_includes.h` 虽然通过 `module.h` 间接引入了 `jump_label.h`，
但 `sucompat.h` 是独立头文件，不能依赖调用方的包含顺序。

**gitignore 冲突**：内核自带 `.gitignore` 有两条会命中补丁文件——
第 107 行 `patches`（quilt）和第 38 行 `*.patch`。已追加
`!/patches/` 与 `!/patches/**/*.patch` 放行。

### 7.2.3 `vfs_statx_fd()` 错误码保护（#3）

`ksu_handle_vfs_fstat()` 内部执行 `*size += ksu_rc_len`，
必须在 `vfs_getattr()` 成功后才调用。失败时 `stat->size` 要么未初始化
（`security_inode_getattr` 阶段就失败），要么被 memset 为 0。

---

## 7.3 未处理项及理由

| 项 | 状态 | 理由 |
|---|---|---|
| KPM 功能无效 | **未处理** | 需先决策：接入 `patch_linux` 内联注入，或关闭 `CONFIG_KPM`。属方案选择，非代码缺陷 |
| NNP 放行钩子 | **未处理** | 必要性未验证，SukiSU builtin 无对应依赖 |
| `kernel/reboot.c` | **不改** | 已论证误重启场景不可能（magic1 互斥） |
| `struct kstat` 的 `mnt_id` | **不改** | SUSFS 的 mnt_id 伪装确实需要；且本树不校验 CRC |
| Actions 缓存 | **未处理** | 纯流水线优化，与正确性无关 |
| `ksu_probe.c` 参数化 | **未处理** | 工具优化，低优先级 |
| 直接产出 boot.img | **搁置** | 用户决定暂缓 |

---

## 7.4 待验证事项

以下需真机或完整编译才能确认：

1. **编译通过**：本次改动涉及 4 个内核文件 + 6 个子模块文件，
   尚未做完整 `make`。风险点：
   - `selinuxfs.c` 中 `handle_sel_write_*` 在 5.4 上现在会被实际调用，
     需确认 `susfs_sel_write_*` 的符号在 `CONFIG_KSU_SUSFS` 下都可见
   - `hooks.c` 中 `susfs_selinux_setprocattr` 同理
2. **SELinux 隐藏效果**：刷入后用 Momo 等工具验证类型存在性探测是否被拦截
3. **静态键行为**：`ksu_su_compat_enabled` 默认 `true`，
   管理器关闭 su_compat 后应能正确 disable

---

## 7.5 变更统计

```
 .github/workflows/build-kernel.yml | 64 +++++-------------------------------
 .gitignore                         |  3 ++
 KernelSU                           |  0
 ci/anykernel.sh                    |  2 +-
 fs/stat.c                          | 10 +++++-
 security/selinux/hooks.c           | 19 +++++++----
 security/selinux/selinuxfs.c       | 12 +++++--
 7 files changed, 47 insertions(+), 63 deletions(-)
```

子模块内 6 个文件改动已固化到
`patches/sukisu-ultra-5.4/0001-suki-pre510-compat.patch`（4894 字节）。

> 注：子模块当前工作区已应用该补丁（`git -C KernelSU status` 显示 6 个 M）。
> 提交时子模块指针不会变化——补丁由 CI 与本地构建脚本在 checkout 后应用。
